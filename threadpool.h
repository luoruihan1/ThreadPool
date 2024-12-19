#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <unordered_map>
#include <thread>

//Any类的实现
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any& other) = delete;
	Any& operator=(const Any& other) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;


	//这个构造函数可以让Any类型接收任意其它类型的数据
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) {}

	//这个cast方法能把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_() {
		//怎么从base_里面找到它所指向的派生类对象，并取出data_成员变量？
		//基类指针转换为派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//基类类型
	class Base {
	public:
		virtual ~Base() = default;
	};
	//派生类类型
	template<typename T>
	class Derive :public Base {
	public:
		Derive(T data) :data_(data) {}
		T data_;
	};
private:
	//定义一个基类的指针
	std::unique_ptr<Base> base_;
};

//实现一个信号量类
class semaphore {
public:
	semaphore(int limit=0) :resLimit_(limit){}
	~semaphore() = default;

	//获取一个信号量资源
	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源的话会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	//增加一个信号量资源
	void post() {
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
//实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	void setVal(Any any);
	//用户调用get方法获取任务执行的结果
	Any get();
private:
	std::atomic_bool isValid_;
	Any any_;//存储result的返回值
	semaphore sem_;//信号量用于告知用户什么时候线程执行完任务
	std::shared_ptr<Task> task_;//指向对应获取返回值的任务对象，因为用户在返回后才会调用task的.get方法获取返回值
	//但是task在submit后对象就析构了，所以result要绑定一个任务对象
	//用shared_ptr指向task并返回，只要指向task的数量不为0就不会析构
};


//任务抽象基类：封装一个任务的抽象基类
//因为我们要用一个容器来存储用户传来的各种各样的任务，因此封装一个基类使得所有子类任务都能被基类任务指向

class Task {
public:
	Task();
		~Task() = default;
	void exec();
	virtual Any run() = 0; //用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	void setResult(Result* res);
private:
	Result* result_;
};


//线程池模式：fixed/cached
enum class PoolMode {
	MODE_FIXED,//固定数量的线程，暂时不需要考虑线程安全模式
	MODE_CACHED,//线程数量可动态增长
};


//线程
class Thread {
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	//线程构造
	Thread(ThreadFunc func);
	//线程析构
	~Thread();
	//启动线程
	void start();
	//获取线程Id
	int getId();
private:
	ThreadFunc func_;
	static int generateId;
	int threadid_;
};


//线程池
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	void start(int initThreadSize = std::thread::hardware_concurrency());//启动线程池(设置初始线程数量=4)
	void setMode(PoolMode mode);//设置线程池模式
	void setTaskQueMaxThreshHold(int threshhold);//设置任务队列中任务数量上限
	Result submitTask(std::shared_ptr<Task> sp);//用户提交任务
	//禁用拷贝构造和拷贝赋值
	ThreadPool(const ThreadPool& other) = delete;
	ThreadPool& operator=(const ThreadPool& other) = delete;
private:
	void threadFunc(int threadId);//定义线程函数

	//检查pool的运行状态
	bool checkRunningState() const;

	//用户可以根据自己的服务器设置最大线程数
	void setMaxThreadNum(int Num) ;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表

	size_t initThreadSize_;//初始线程数量
	std::atomic_int nowThreadNum_;//当前线程池中线程总数（工作+空闲）
	int threadSizeThreshHold_;//线程最大数量上限阈值（初始+添加的）
	std::atomic_int idleThreadSize_;//记录当前线程队列中空闲线程的数量

	//任务队列，这里用智能指针是因为用户传进来的任务对象可能只是一个临时任务对象，因此这里采用智能指针来管理任务对象的生命周期，不让他提前释放
	std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
	std::atomic_uint taskSize_; //无符号整数uint，任务的数量
	int taskQueThreshHold_;//任务队列中能容纳的任务上限
	std::mutex taskQueMtx_;//任务队列锁，保证任务队列的线程安全（因为线程池和外部用户可能同时对任务队列进行读写操作，这是一个典型的生产者消费者模型）

	//条件变量的逻辑：1 如果任务队列不空，消费者（线程池）可以从任务队列取任务 2 如果任务队列不满，生产者（用户）可以添加任务到任务队列中
	std::condition_variable notFull;//条件变量：任务队列不空
	std::condition_variable notEmpty;//条件变量：任务队列不满
	std::condition_variable exitCond_;//等待线程资源全部回收

	PoolMode poolMode_; //当前线程池的工作模式
	std::atomic_bool isPoolRunning_;//表示当前线程池启动状态

	

};

#endif; // !THREADPOOL_H