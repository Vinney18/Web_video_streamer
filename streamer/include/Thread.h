
#include <thread>
#include <mutex>
#include <condition_variable>

#pragma once
class Thread {

protected:
	std::thread mThread;                ///< Base std::thread
	std::mutex mThreadMutex;            ///< Mutex protecting this thread from concurrent access (of methods like start, join)
	std::condition_variable cv;

	bool  mRunning;                     ///< Flag indicating whether this thread has begun and is running
	bool  mStop;                        ///< Flag indicating whether this thread has been signalled to stop

protected:
	Thread();

	virtual ~Thread();

	std::thread::id id() const
	{
		return mThread.get_id();
	}

public:
	virtual int run() = 0;        ///< Thread code, must be overriden to do actual thread work

	std::thread::id tid() const   ///< Return the thread id
	{
		return mThread.get_id();
	}

	void startThread();               ///< Start the thread and invoke the run method, called by invoker
	virtual void stopThread();        ///< Stop the thread, by setting the mStop flag. Thread is responsible for actually existing.
	void join();                ///< Wait for the thread to terminate.

	bool running() const        ///< Indicate whether the thread is running or not
	{
		return(mRunning);
	}

	bool stopped() const        ///< Indicate whether the thread has been signalled to stop
	{
		return(mStop);
	}

};
