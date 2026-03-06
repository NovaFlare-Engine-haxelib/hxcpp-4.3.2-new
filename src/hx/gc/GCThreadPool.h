#ifndef HX_GC_THREAD_POOL_H
#define HX_GC_THREAD_POOL_H

#include <hx/Thread.h>
#include <vector>
#include <deque>
#include <stdio.h>

namespace hx {

class GCTask {
public:
   virtual ~GCTask() {}
   virtual void Execute(int inThreadId) = 0;
};

class GCThreadPool {
public:
   struct WorkerContext {
      GCThreadPool *pool;
      int id;
   };

   GCThreadPool(int inThreadCount) : mThreadCount(inThreadCount)
   {
      mRunning = true;
      mActiveTasks = 0;
      
      for(int i=0; i<mThreadCount; i++)
      {
         WorkerContext *ctx = new WorkerContext();
         ctx->pool = this;
         ctx->id = i;
         HxCreateDetachedThread( RunWorker, ctx );
      }
   }

   ~GCThreadPool()
   {
      Shutdown();
   }

   static THREAD_FUNC_TYPE RunWorker(void *inCtx)
   {
      WorkerContext *ctx = (WorkerContext *)inCtx;
      ctx->pool->WorkerLoop(ctx->id);
      THREAD_FUNC_RET
   }

   void AddTask(GCTask *inTask)
   {
      AutoLock lock(mMutex);
      mQueue.push_back(inTask);
      mActiveTasks++;
      mWorkSemaphore.Set();
   }

   void WaitAll()
   {
      while(true)
      {
         mMutex.Lock();
         bool done = (mActiveTasks == 0 && mQueue.empty());
         mMutex.Unlock();
         
         if (done) return;
         
         mDoneSemaphore.Wait();
      }
   }

   void Shutdown()
   {
      {
         AutoLock lock(mMutex);
         if (!mRunning) return;
         mRunning = false;
      }
      
      // Wake up all threads
      for(int i=0; i<mThreadCount; i++)
         mWorkSemaphore.Set();
   }

private:
   void WorkerLoop(int inId)
   {
      while(true)
      {
         GCTask *task = 0;
         {
            AutoLock lock(mMutex);
            if (!mRunning) break;
            if (!mQueue.empty())
            {
               task = mQueue.front();
               mQueue.pop_front();
               
               // If there are more tasks, wake up another thread
               if (!mQueue.empty())
                  mWorkSemaphore.Set();
            }
         }

         if (task)
         {
            task->Execute(inId);
            delete task;

            AutoLock lock(mMutex);
            mActiveTasks--;
            if (mActiveTasks == 0 && mQueue.empty())
            {
               mDoneSemaphore.Set();
            }
         }
         else
         {
            // No task, wait for signal
            mWorkSemaphore.Wait();
         }
      }
   }

   int mThreadCount;
   bool mRunning;
   int mActiveTasks;
   
   std::deque<GCTask *> mQueue;

   HxMutex mMutex;
   HxSemaphore mWorkSemaphore;
   HxSemaphore mDoneSemaphore;
};

} // namespace hx

#endif
