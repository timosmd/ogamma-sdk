// The class is originally downloaded from github: https://github.com/progschj/ThreadPool.git
// Modifications made: 
// 1. Included into OWA::OpcUa namespace

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <iostream>
#include "opcua/CriticalError.h"

namespace OWA {
	namespace OpcUa {
		class ThreadPool {
		public:
			ThreadPool(size_t);
			template<class F, class... Args>
			auto enqueue(F&& f, Args&&... args)
				->std::future<typename std::result_of<F(Args...)>::type>;
			
			//template<class F, class... Args>
			//void enqueue(F&& f, Args&&... args);

			~ThreadPool();
		private:
			// need to keep track of threads so we can join them
			std::vector< std::thread > workers;
			// the task queue
			std::queue<std::function<void()>> tasks;

			// synchronization
			std::mutex queue_mutex;
			std::condition_variable condition;
			bool stop;
		};
		// the constructor just launches some amount of workers
		inline ThreadPool::ThreadPool(size_t threads)
			: stop(false)
		{
			if (threads < 1)
			{
				throw CriticalErrorException("ThreadPool: Invalid argument ""threads""");
			}
			for (size_t i = 0; i < threads; ++i)
				workers.emplace_back(
					[this] {
						for (;;)
						{
							std::function<void()> task;
							{
								std::unique_lock<std::mutex> lock(this->queue_mutex);
								this->condition.wait(lock,
									[this]
								{
									return this->stop || !this->tasks.empty();
								});
								if (this->stop && this->tasks.empty()) {
									return;
								}
								task = this->tasks.front();
								this->tasks.pop();
								if (tasks.size() >= 10)
								{
									std::cout << "Pop - Thread pool task queue size is " << tasks.size() << "\n";
								}
							}
							task();
						}
					}
				);
		}

		template<class F, class... Args>
		auto ThreadPool::enqueue(F&& f, Args&&... args)
			-> std::future<typename std::result_of<F(Args...)>::type>
		{
			using return_type = typename std::result_of<F(Args...)>::type;

			auto task = std::make_shared< std::packaged_task<return_type()> >(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...)
				);

			std::future<return_type> res = task->get_future();
			std::function<void()> func = [task]() mutable {
				(*task)();
				task.reset();
			};

			{
				std::unique_lock<std::mutex> lock(queue_mutex);

				// don't allow enqueueing after stopping the pool
				if (stop)
					throw CriticalErrorException("enqueue on stopped ThreadPool");

				tasks.emplace(func);
			}
			condition.notify_one();
			return res;
		}

		// the destructor joins all threads
		inline ThreadPool::~ThreadPool()
		{
			{
				std::unique_lock<std::mutex> lock(queue_mutex);
				stop = true;
			}
			condition.notify_all();
			for (std::thread &worker : workers)
				worker.join();
		}
	}
}
#endif