#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
const int TASK_MAX_THRESHHOLD = 200; //任务(队列)最多数量
const int THREAD_MAX_THRESHHOLD = 100;//线程最多数量
const int THREAD_MAX_IDLE_TIME = 10;//线程最大空闲时间	

//线程池构造
ThreadPool::ThreadPool():initThreadSize_(0), taskSize_(0)
, taskQueThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(PoolMode::MODE_FIXED),isPoolRunning_(false)
, idleThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), nowThreadNum_(0)
{

}

//线程池析构
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	//notEmpty.notify_all();//通知所有线程(唤醒所有线程) 这是为了不让所有线程等在60s那
	//然后线程会过来获取锁，exitCond_：如果threads_的size不为0，线程释放锁，在这里等着

	//等待线程池里面所有的线程返回
	//此时线程有两种状态1 阻塞在notEmpty上数60s的（没有任务的）2 执行任务的
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty.notify_all();//通知所有线程(唤醒所有线程) 这是为了不让所有线程等在60s那
	//把notEmpty移动到锁后，解决死锁问题
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	std::cout << "threads_.size() == 0" << std::endl;
}

//设置线程池模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) {
		std::cerr << "thread already start,can not change mode!" << std::endl;
		return;
	}
	poolMode_ = mode;
}

//设置任务队列中任务数量上限
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) {
		std::cerr << "thread already start,can not set task max threshhold!" << std::endl;
		return;
	}
	taskQueThreshHold_ = threshhold;
}

//用户提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//用户调用该接口，传入任务对象，生产任务
	//获取任务队列锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//线程的通信 等待任务队列有空余
	
		//如果任务队列已满
		//当前线程进入等待状态，直到任务队列不满
	//如果当前任务队列中任务的数量 < 最大任务数量上限，那么往下走，否则阻塞在这并释放任务队列锁lock
	//notFull.wait(lock, [&]()->bool {return taskQue_.size() < taskQueThreshHold_; });//wait：一直阻塞，直到条件满足
	//wait_for()：在一段时间内等待，超时则返回false；wait_until：在一个时间点之前等待，到了这个时间点则返回失败
	if (!notFull.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueThreshHold_; })) //优化：用户最长不能阻塞超过1s，超时判断任务提交失败，返回false
	{
		//如果返回false，代表超时
		std::cerr << "task queue is full, submit task fail. " << std::endl;
		return Result(sp,false); //返回一个任务对象，一个提交失败
	}
		//如果有空余，把任务放入任务队列中，任务数量++
	taskQue_.emplace(sp);
	taskSize_++;

	//notEmpty()通知，此时任务队列不空
	notEmpty.notify_all();

	//cached模式需要根据任务数量和空闲线程的数量判断是否需要创建新的线程
	//cached模式适用于小而快的任务，比如紧急任务
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_
		&& nowThreadNum_ < threadSizeThreshHold_) {
		//如果当前模式为cached，且当前任务数>当前空闲线程数，且当前线程总数<线程数量上限
		//那么就再创建n个线程以处理用户任务
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		//threads_.emplace(std::move(ptr));//将指向线程类的智能指针添加到线程队列threads_中
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		
		nowThreadNum_++;
		idleThreadSize_++;
		std::cout << "start new thread successfully,now we have " << nowThreadNum_ <<" threads" << std::endl;
	}

	//返回任务的result对象
	return Result(sp, true); //返回一个任务对象，一个提交成功
}

//开启线程池
void ThreadPool::start(int initThreadSize) {
	initThreadSize_ = initThreadSize; //由用户决定池中开辟多少个线程
	nowThreadNum_ = initThreadSize;//当前总线程数量
	isPoolRunning_ = true;
	//创建线程对象
	for (int i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));//采用智能指针unique_ptr取代new出来的普通指针,std::placeholders::_1是一个参数占位符
		//注意，这里unique_ptr是不允许进行普通的拷贝构造和赋值的，因此用move把资源转移
		//threads_.emplace_back(std::move(ptr));//将指向线程类的智能指针添加到线程队列threads_中
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	//启动所有线程
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;//每启动一个线程，空闲线程++
	}
}

//定义线程函数 线程池的消费者线程，负责从任务队列中取任务
void ThreadPool::threadFunc(int threadId) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> task;
		{
	//获取任务队列锁
			std::cout << "thread " << threadId << " trying to get lock" << std::endl;
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		std::cout << "thread " << threadId << " get lock success" << std::endl;
		//cache模式下，超出initThreadSize_数量的，超过60s没有执行的线程需要被回收
		 
			//每1s返回一次，区分超时返回和有任务待执行返回
			while (taskQue_.size() == 0) {
				//如果已经没有任务了
				if (!isPoolRunning_) {
					//如果没有任务了 且调用了析构，回收线程，否则等待
					threads_.erase(threadId);
std::cout << "while end,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
exitCond_.notify_all(); //线程回收完后要通知exitCond_，不然会阻塞在析构的exitCond_上
return;
}
				//如果没有任务 开始计算线程空闲时间 60s如果线程还没有处理任务，则earase掉多余线程
				if (poolMode_ == PoolMode::MODE_CACHED) {
				//条件变量：超时返回
				if (std::cv_status::timeout ==
					notEmpty.wait_for(lock, std::chrono::seconds(1))) {
					//这里有个问题：当线程池析构时，如果没有任务，此时所有线程都会阻塞在这里，所以需要手动析构线程
					auto now = std::chrono::high_resolution_clock().now();
					auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
					//
					if (dur.count() > THREAD_MAX_IDLE_TIME && nowThreadNum_ > initThreadSize_) {
						//回收当前线程
						//记录线程数量的值修改
						//线程队列中的线程对象释放 难点：怎么从这个函数找到当前线程对象呢？
						//解决：把线程容器改为map，建立映射关系：线程id=》线程对象=》删除
						threads_.erase(threadId);
						nowThreadNum_--;
						idleThreadSize_--;
						std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
						return;
					}
				}
				}
				else {
	//等待notEmpty条件（任务队列没有任务 则等待，即在这里阻塞，因此fixed模式也需要再pool的析构处唤醒线程）
		notEmpty.wait(lock);
				}
				//当在notEmpty被唤醒时，有两种情况：1 新任务到来。2 pool调用析构
				//if (!isPoolRunning_) {
				//	//如果是pool调用析构 回收线程资源
				//	threads_.erase(threadId);
				//	nowThreadNum_--;
				//	idleThreadSize_--;
				//	
				//	std::cout << "pool exit,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}
			//如果这么写 可以解决死锁 但是不能解决先完成任务在析构
			// 更改后：有任务的话，不管是否已经调用了析构，都先执行任务
			//if (!isPoolRunning_) {
			//	//这里是为了防止死锁
			//	break;
			//}
	//满足条件时，空闲线程数量--
		idleThreadSize_--;
	//一个线程从任务队列中取一个任务出来
		task = taskQue_.front();
		taskQue_.pop();
	//任务队列中任务数量-- 
		taskSize_--;

	//如果依然有剩余任务，继续通知其他线程执行任务
		if (taskQue_.size() > 0) {
			notEmpty.notify_all();
		}
	//取出一个任务 进行通知
		notFull.notify_all();
		}//释放锁
		std::cout << "thread " << threadId << " release lock" << std::endl;
	//线程执行任务
		if (task !=nullptr) {
			task->exec();
		}
		//线程执行完任务，空闲线程++
		idleThreadSize_++;
		//更新线程执行完任务的时间
		lastTime = std::chrono::high_resolution_clock().now();
	}
	//threads_.erase(threadId);
	//std::cout << "while end,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
	//exitCond_.notify_all(); //线程回收完后要通知exitCond_，不然会阻塞在析构的exitCond_上
	//return;
}

//检查pool的运行状态
bool ThreadPool::checkRunningState() const {
//	if (isPoolRunning_) {
//	std::cerr << "thread already start,can not change mode!" << std::endl;
//	return;
//}
	return isPoolRunning_;
}

//用户可以根据自己的服务器设置最大线程数
void ThreadPool::setMaxThreadNum(int Num)  {
	if (checkRunningState()) {
		std::cerr << "thread already start,can not set thread max Num!" << std::endl;
		return;
	}
	if (poolMode_ == PoolMode::MODE_FIXED) {
		std::cerr << "FIXED_MODE can not change thread Num,they are fixed!" << std::endl;
		return;
	}
	threadSizeThreshHold_ = Num;
}

Task::Task() :
	result_(nullptr)
{

}

void Task::exec() {
	if (result_!=nullptr) {
     result_->setVal(run());
	}
	
}

void Task::setResult(Result* res) {
	result_ = res;
}

int Thread::generateId = 0;
//线程构造
Thread::Thread(ThreadFunc func) 
	:func_(func), threadid_(generateId++)
{

}
//线程析构
Thread::~Thread() {

}
//启动线程
void Thread::start() {
	//创建一个线程去执行一个线程函数
	std::thread t(func_,threadid_);
	t.detach();//设置分离线程
}
//获取线程Id
int Thread::getId() {
	return threadid_;
}


Result::Result(std::shared_ptr<Task> task,bool isValid) :
	task_(task),isValid_(isValid)
{
	task_->setResult(this);
}

Any Result::get() {
	if (!isValid_) {
		return "";
	}
	sem_.wait();//如果任务没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

void Result::setVal(Any any) {
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();//已经获取的任务的返回值，增加信号量资源
}