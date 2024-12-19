// AnyThreadPool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "threadpool.h"
#include <chrono>
#include <thread>
using namespace std;
//测试：
class MyTask : public Task {
public:
	MyTask(int begin,int end):
		begin_(begin),end_(end)
	{}
	Any run() {
		cout << "tid: " << this_thread::get_id() << " begin!" << endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		

		int sum = 0;
		for (auto i = begin_; i < end_; i++) {
			sum += i;
		}

		cout << "tid: " << this_thread::get_id() << " finish job with sum: " << sum << endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};




int main()
{


	{
		//pool析构以后怎么把线程资源全部回收
		//因为没人submit了，所有线程都在等待条件变量，不会释放
		//这时候需要在pool的析构函数手动结束掉线程
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(3);
		//pool.setMode(PoolMode::MODE_CACHED);
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 2000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(2000, 3000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(3000, 4001));
		pool.submitTask(std::make_shared<MyTask>(1, 2000));
		pool.submitTask(std::make_shared<MyTask>(1, 2000));
		pool.submitTask(std::make_shared<MyTask>(1, 2000));

		int sum1 = res1.get().cast_<int>();
		int sum2 = res2.get().cast_<int>();
		int sum3 = res3.get().cast_<int>();

		cout << (sum1 + sum2 + sum3) << endl;

		//std::this_thread::sleep_for(std::chrono::seconds(50));
		getchar();
	}

}

