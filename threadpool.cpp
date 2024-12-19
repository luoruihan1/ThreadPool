#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
const int TASK_MAX_THRESHHOLD = 200; //����(����)�������
const int THREAD_MAX_THRESHHOLD = 100;//�߳��������
const int THREAD_MAX_IDLE_TIME = 10;//�߳�������ʱ��	

//�̳߳ع���
ThreadPool::ThreadPool():initThreadSize_(0), taskSize_(0)
, taskQueThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(PoolMode::MODE_FIXED),isPoolRunning_(false)
, idleThreadSize_(0), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), nowThreadNum_(0)
{

}

//�̳߳�����
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	//notEmpty.notify_all();//֪ͨ�����߳�(���������߳�) ����Ϊ�˲��������̵߳���60s��
	//Ȼ���̻߳������ȡ����exitCond_�����threads_��size��Ϊ0���߳��ͷ��������������

	//�ȴ��̳߳��������е��̷߳���
	//��ʱ�߳�������״̬1 ������notEmpty����60s�ģ�û������ģ�2 ִ�������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty.notify_all();//֪ͨ�����߳�(���������߳�) ����Ϊ�˲��������̵߳���60s��
	//��notEmpty�ƶ������󣬽����������
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	std::cout << "threads_.size() == 0" << std::endl;
}

//�����̳߳�ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState()) {
		std::cerr << "thread already start,can not change mode!" << std::endl;
		return;
	}
	poolMode_ = mode;
}

//�������������������������
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState()) {
		std::cerr << "thread already start,can not set task max threshhold!" << std::endl;
		return;
	}
	taskQueThreshHold_ = threshhold;
}

//�û��ύ����
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//�û����øýӿڣ��������������������
	//��ȡ���������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//�̵߳�ͨ�� �ȴ���������п���
	
		//��������������
		//��ǰ�߳̽���ȴ�״̬��ֱ��������в���
	//�����ǰ������������������ < ��������������ޣ���ô�����ߣ������������Ⲣ�ͷ����������lock
	//notFull.wait(lock, [&]()->bool {return taskQue_.size() < taskQueThreshHold_; });//wait��һֱ������ֱ����������
	//wait_for()����һ��ʱ���ڵȴ�����ʱ�򷵻�false��wait_until����һ��ʱ���֮ǰ�ȴ����������ʱ����򷵻�ʧ��
	if (!notFull.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueThreshHold_; })) //�Ż����û��������������1s����ʱ�ж������ύʧ�ܣ�����false
	{
		//�������false������ʱ
		std::cerr << "task queue is full, submit task fail. " << std::endl;
		return Result(sp,false); //����һ���������һ���ύʧ��
	}
		//����п��࣬�����������������У���������++
	taskQue_.emplace(sp);
	taskSize_++;

	//notEmpty()֪ͨ����ʱ������в���
	notEmpty.notify_all();

	//cachedģʽ��Ҫ�������������Ϳ����̵߳������ж��Ƿ���Ҫ�����µ��߳�
	//cachedģʽ������С��������񣬱����������
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_
		&& nowThreadNum_ < threadSizeThreshHold_) {
		//�����ǰģʽΪcached���ҵ�ǰ������>��ǰ�����߳������ҵ�ǰ�߳�����<�߳���������
		//��ô���ٴ���n���߳��Դ����û�����
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		//threads_.emplace(std::move(ptr));//��ָ���߳��������ָ����ӵ��̶߳���threads_��
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		
		nowThreadNum_++;
		idleThreadSize_++;
		std::cout << "start new thread successfully,now we have " << nowThreadNum_ <<" threads" << std::endl;
	}

	//���������result����
	return Result(sp, true); //����һ���������һ���ύ�ɹ�
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) {
	initThreadSize_ = initThreadSize; //���û��������п��ٶ��ٸ��߳�
	nowThreadNum_ = initThreadSize;//��ǰ���߳�����
	isPoolRunning_ = true;
	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));//��������ָ��unique_ptrȡ��new��������ָͨ��,std::placeholders::_1��һ������ռλ��
		//ע�⣬����unique_ptr�ǲ����������ͨ�Ŀ�������͸�ֵ�ģ������move����Դת��
		//threads_.emplace_back(std::move(ptr));//��ָ���߳��������ָ����ӵ��̶߳���threads_��
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	//���������߳�
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;//ÿ����һ���̣߳������߳�++
	}
}

//�����̺߳��� �̳߳ص��������̣߳���������������ȡ����
void ThreadPool::threadFunc(int threadId) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> task;
		{
	//��ȡ���������
			std::cout << "thread " << threadId << " trying to get lock" << std::endl;
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		std::cout << "thread " << threadId << " get lock success" << std::endl;
		//cacheģʽ�£�����initThreadSize_�����ģ�����60sû��ִ�е��߳���Ҫ������
		 
			//ÿ1s����һ�Σ����ֳ�ʱ���غ��������ִ�з���
			while (taskQue_.size() == 0) {
				//����Ѿ�û��������
				if (!isPoolRunning_) {
					//���û�������� �ҵ����������������̣߳�����ȴ�
					threads_.erase(threadId);
std::cout << "while end,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
exitCond_.notify_all(); //�̻߳������Ҫ֪ͨexitCond_����Ȼ��������������exitCond_��
return;
}
				//���û������ ��ʼ�����߳̿���ʱ�� 60s����̻߳�û�д���������earase�������߳�
				if (poolMode_ == PoolMode::MODE_CACHED) {
				//������������ʱ����
				if (std::cv_status::timeout ==
					notEmpty.wait_for(lock, std::chrono::seconds(1))) {
					//�����и����⣺���̳߳�����ʱ�����û�����񣬴�ʱ�����̶߳������������������Ҫ�ֶ������߳�
					auto now = std::chrono::high_resolution_clock().now();
					auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
					//
					if (dur.count() > THREAD_MAX_IDLE_TIME && nowThreadNum_ > initThreadSize_) {
						//���յ�ǰ�߳�
						//��¼�߳�������ֵ�޸�
						//�̶߳����е��̶߳����ͷ� �ѵ㣺��ô����������ҵ���ǰ�̶߳����أ�
						//��������߳�������Ϊmap������ӳ���ϵ���߳�id=���̶߳���=��ɾ��
						threads_.erase(threadId);
						nowThreadNum_--;
						idleThreadSize_--;
						std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
						return;
					}
				}
				}
				else {
	//�ȴ�notEmpty�������������û������ ��ȴ��������������������fixedģʽҲ��Ҫ��pool�������������̣߳�
		notEmpty.wait(lock);
				}
				//����notEmpty������ʱ�������������1 ����������2 pool��������
				//if (!isPoolRunning_) {
				//	//�����pool�������� �����߳���Դ
				//	threads_.erase(threadId);
				//	nowThreadNum_--;
				//	idleThreadSize_--;
				//	
				//	std::cout << "pool exit,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}
			//�����ôд ���Խ������ ���ǲ��ܽ�����������������
			// ���ĺ�������Ļ��������Ƿ��Ѿ�����������������ִ������
			//if (!isPoolRunning_) {
			//	//������Ϊ�˷�ֹ����
			//	break;
			//}
	//��������ʱ�������߳�����--
		idleThreadSize_--;
	//һ���̴߳����������ȡһ���������
		task = taskQue_.front();
		taskQue_.pop();
	//�����������������-- 
		taskSize_--;

	//�����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
		if (taskQue_.size() > 0) {
			notEmpty.notify_all();
		}
	//ȡ��һ������ ����֪ͨ
		notFull.notify_all();
		}//�ͷ���
		std::cout << "thread " << threadId << " release lock" << std::endl;
	//�߳�ִ������
		if (task !=nullptr) {
			task->exec();
		}
		//�߳�ִ�������񣬿����߳�++
		idleThreadSize_++;
		//�����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
	}
	//threads_.erase(threadId);
	//std::cout << "while end,threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
	//exitCond_.notify_all(); //�̻߳������Ҫ֪ͨexitCond_����Ȼ��������������exitCond_��
	//return;
}

//���pool������״̬
bool ThreadPool::checkRunningState() const {
//	if (isPoolRunning_) {
//	std::cerr << "thread already start,can not change mode!" << std::endl;
//	return;
//}
	return isPoolRunning_;
}

//�û����Ը����Լ��ķ�������������߳���
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
//�̹߳���
Thread::Thread(ThreadFunc func) 
	:func_(func), threadid_(generateId++)
{

}
//�߳�����
Thread::~Thread() {

}
//�����߳�
void Thread::start() {
	//����һ���߳�ȥִ��һ���̺߳���
	std::thread t(func_,threadid_);
	t.detach();//���÷����߳�
}
//��ȡ�߳�Id
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
	sem_.wait();//�������û��ִ���꣬����������û����߳�
	return std::move(any_);
}

void Result::setVal(Any any) {
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();//�Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}