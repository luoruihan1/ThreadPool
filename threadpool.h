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

//Any���ʵ��
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any& other) = delete;
	Any& operator=(const Any& other) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;


	//������캯��������Any���ͽ��������������͵�����
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) {}

	//���cast�����ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_() {
		//��ô��base_�����ҵ�����ָ�����������󣬲�ȡ��data_��Ա������
		//����ָ��ת��Ϊ������ָ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base {
	public:
		virtual ~Base() = default;
	};
	//����������
	template<typename T>
	class Derive :public Base {
	public:
		Derive(T data) :data_(data) {}
		T data_;
	};
private:
	//����һ�������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź�����
class semaphore {
public:
	semaphore(int limit=0) :resLimit_(limit){}
	~semaphore() = default;

	//��ȡһ���ź�����Դ
	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	//����һ���ź�����Դ
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
//ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	void setVal(Any any);
	//�û�����get������ȡ����ִ�еĽ��
	Any get();
private:
	std::atomic_bool isValid_;
	Any any_;//�洢result�ķ���ֵ
	semaphore sem_;//�ź������ڸ�֪�û�ʲôʱ���߳�ִ��������
	std::shared_ptr<Task> task_;//ָ���Ӧ��ȡ����ֵ�����������Ϊ�û��ڷ��غ�Ż����task��.get������ȡ����ֵ
	//����task��submit�����������ˣ�����resultҪ��һ���������
	//��shared_ptrָ��task�����أ�ֻҪָ��task��������Ϊ0�Ͳ�������
};


//���������ࣺ��װһ������ĳ������
//��Ϊ����Ҫ��һ���������洢�û������ĸ��ָ�����������˷�װһ������ʹ���������������ܱ���������ָ��

class Task {
public:
	Task();
		~Task() = default;
	void exec();
	virtual Any run() = 0; //�û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	void setResult(Result* res);
private:
	Result* result_;
};


//�̳߳�ģʽ��fixed/cached
enum class PoolMode {
	MODE_FIXED,//�̶��������̣߳���ʱ����Ҫ�����̰߳�ȫģʽ
	MODE_CACHED,//�߳������ɶ�̬����
};


//�߳�
class Thread {
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	//�̹߳���
	Thread(ThreadFunc func);
	//�߳�����
	~Thread();
	//�����߳�
	void start();
	//��ȡ�߳�Id
	int getId();
private:
	ThreadFunc func_;
	static int generateId;
	int threadid_;
};


//�̳߳�
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	void start(int initThreadSize = std::thread::hardware_concurrency());//�����̳߳�(���ó�ʼ�߳�����=4)
	void setMode(PoolMode mode);//�����̳߳�ģʽ
	void setTaskQueMaxThreshHold(int threshhold);//�������������������������
	Result submitTask(std::shared_ptr<Task> sp);//�û��ύ����
	//���ÿ�������Ϳ�����ֵ
	ThreadPool(const ThreadPool& other) = delete;
	ThreadPool& operator=(const ThreadPool& other) = delete;
private:
	void threadFunc(int threadId);//�����̺߳���

	//���pool������״̬
	bool checkRunningState() const;

	//�û����Ը����Լ��ķ�������������߳���
	void setMaxThreadNum(int Num) ;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�

	size_t initThreadSize_;//��ʼ�߳�����
	std::atomic_int nowThreadNum_;//��ǰ�̳߳����߳�����������+���У�
	int threadSizeThreshHold_;//�߳��������������ֵ����ʼ+��ӵģ�
	std::atomic_int idleThreadSize_;//��¼��ǰ�̶߳����п����̵߳�����

	//������У�����������ָ������Ϊ�û�������������������ֻ��һ����ʱ���������������������ָ�����������������������ڣ���������ǰ�ͷ�
	std::queue<std::shared_ptr<Task>> taskQue_; //�������
	std::atomic_uint taskSize_; //�޷�������uint�����������
	int taskQueThreshHold_;//��������������ɵ���������
	std::mutex taskQueMtx_;//�������������֤������е��̰߳�ȫ����Ϊ�̳߳غ��ⲿ�û�����ͬʱ��������н��ж�д����������һ�����͵�������������ģ�ͣ�

	//�����������߼���1 ���������в��գ������ߣ��̳߳أ����Դ��������ȡ���� 2 ���������в����������ߣ��û�����������������������
	std::condition_variable notFull;//����������������в���
	std::condition_variable notEmpty;//����������������в���
	std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

	PoolMode poolMode_; //��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_;//��ʾ��ǰ�̳߳�����״̬

	

};

#endif; // !THREADPOOL_H