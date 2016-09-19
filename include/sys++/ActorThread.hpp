// Active Object pattern wrapping a standard C++11 thread (https://github.com/lightful/syscpp)
//
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
/*
 - Publicly inherit from this template (specializing it for the derived class itself)
 - On the derived (active object) class everything should be private: make the base a friend
 - Instance the active object by invoking the inherited public static create() or run() methods
 - Use send() to send messages (of any data type) to the active object
 - Use transfer() to move messages to the active object (e.g. wrapped in unique_ptr)
 - Use onMessage(AnyType&) methods to implement the messages reception on the active object
 - Optionally build Channel objects or use a Gateway wrapper instead of send() or transfer()
 - Optionally override onStart() and onStop() in the active object
 - Optionally use connect() from unknown clients to bind callbacks for any data type
 - Optionally use publish() from the active object to invoke the binded callbacks
 - Optionally use timerStart() / timerStop() / timerReset() from the active object
 */
#ifndef ACTORTHREAD_H_
#define ACTORTHREAD_H_

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <set>
#include <map>

template <typename Runnable> class ActorThread : public std::enable_shared_from_this<Runnable>
{
    public:

        typedef std::shared_ptr<Runnable> ptr;

        template <typename ... Args> static ptr create(Args&&... args) // spawn a new thread
        {
            auto task = ptr(new Runnable(std::forward<Args>(args)...), actorThreadRecycler);
            task->runner = std::thread(&ActorThread::dispatcher, task.get());
            return task;
        }

        template <typename ... Args> static int run(Args&&... args) // run in the calling thread (e.g. main() thread)
        {
            struct ActRunTask : public Runnable { ActRunTask(Args&&... arg) : Runnable(std::forward<Args>(arg)...) {} };
            return std::make_shared<ActRunTask>(std::forward<Args>(args)...)->dispatcher();
        }

        template <bool HighPri = false, typename Any> inline void send(const Any& msg) // polymorphic message passing
        {
            post<HighPri>(std::unique_ptr<Message<Any>>(new Message<Any>(msg)));
        }

        template <bool HighPri = false, typename Any> inline void transfer(Any& msg) // ownership transfer version
        {
            post<HighPri>(std::unique_ptr<Message<Any>>(new Message<Any>(std::move(msg))));
        }

        template <typename Any> using Channel = std::function<void(const Any&)>;

        template <typename Any, bool HighPri = false> Channel<Any> getChannel() // build a generic callback
        {
            std::weak_ptr<ActorThread> weakBind(this->shared_from_this());  // Note:
            return Channel<Any>([weakBind](const Any& data)                 // std::bind can't store a weak_ptr
            {                                                               // in std::function (and a shared_ptr
                auto aliveTarget = weakBind.lock();                         // would prevent objects destruction)
                if (aliveTarget) aliveTarget->template send<HighPri>(data);
            });
        }

        template <typename Any> void connect(Channel<Any> receiver = Channel<Any>()) // bind (or unbind) a generic callback
        {
            post<true>(std::unique_ptr<Callback<Any>>(new Callback<Any>(std::forward<Channel<Any>>(receiver))));
        }

        template <typename Any, bool HighPri = false, typename Him> void connect(const std::shared_ptr<Him>& receiver)
        {
            connect(receiver->template getChannel<Any, HighPri>()); // bind another ActorThread
        }

        std::size_t pendingMessages() const // amount of undispatched messages in the active object
        {
            std::lock_guard<std::mutex> ulock(mtx);
            return mboxNormPri.size() + mboxHighPri.size();
        }

        typedef std::chrono::steady_clock TimerClock;

        void waitIdle(TimerClock::duration maxWait = std::chrono::seconds(1)) // blocks until there aren't pending messages
        {
            std::unique_lock<std::mutex> ulock(mtx);
            if (!(mboxNormPri.empty() && mboxHighPri.empty())) idleWaiter.wait_until(ulock, TimerClock::now() + maxWait);
        }

        void stop() // optional (suffices deleting the object): works if created by run() or invoked from ANOTHER thread
        {
            stop(false);
        }

        bool exiting() const // mostly to allow active objects running intensive jobs to poll for a shutdown request
        {
            std::lock_guard<std::mutex> ulock(mtx);
            return !dispatching;
        }

        struct Gateway // safe wrapper for instances of unknown lifecycle
        {
            Gateway(const std::weak_ptr<Runnable>& actorThread = ptr()) : actor(actorThread) {}

            template <bool HighPri = false, typename Any> inline void send(const Any& msg) const
            {
                auto aliveTarget = get();
                if (aliveTarget) aliveTarget->template send<HighPri>(msg);
            }

            template <bool HighPri = false, typename Any> inline void transfer(Any& msg) const
            {
                auto aliveTarget = get();
                if (aliveTarget) aliveTarget->template transfer<HighPri>(msg);
            }

            template <typename Any> inline void operator()(const Any& msg) const // handy function-like syntax
            {
                send(msg);
            }

            void set(const std::weak_ptr<Runnable>& actorThread) { actor = actorThread; }

            inline ptr get() const { return actor.lock(); }

            private: std::weak_ptr<Runnable> actor;
        };

    protected:

        explicit ActorThread() : dispatching(true), detached(false), mboxPaused(false) {}

        virtual ~ActorThread() {} // messages pending to be dispatched are discarded

        /* methods invoked on the active object (this default implementation can be "overrided") */

        void onStart() {}
        int onStop() { return 0; } // the supplied exit code is returned from run()

        /* the active object may use this family of methods to perform the callbacks onto connected clients */

        template <typename Any> static Channel<Any>& publish(const Any& value, bool deliver = true)
        {
            static thread_local Channel<Any> bearer; // callback storage (per-thread and type)
            if (bearer && deliver) bearer(value); // if binded with getChannel() won't call a deleted peer
            return bearer;
        }

        template <typename Any> inline static Channel<Any>& callback() // helper function to fetch the stored callbacks
        {
            const Any* nothing = nullptr;
            return publish(*nothing, false);
        }

        /* timers facility for the active object (unlimited amount: one per each "payload" instance) */

        enum class TimerCycle { Periodic, OneShot };

        template <typename Any> void timerStart(const Any& payload, TimerClock::duration lapse,
                                                Channel<Any> event, TimerCycle cycle = TimerCycle::OneShot)
        {
            std::shared_ptr<TimerEvent<Any>> timer;
            auto& allTimersOfThatType = timerEvents<Any>(this);
            auto pTimer = allTimersOfThatType.find(payload);
            if (pTimer == allTimersOfThatType.end())
            {
                timer = std::make_shared<TimerEvent<Any>>(std::forward<Channel<Any>>(event), payload);
                allTimersOfThatType.emplace(timer->payload, timer);
            }
            else // reschedule and reprogram
            {
                timer = pTimer->second.lock();
                timers.erase(timer);
            }
            timer->lapse = lapse;
            timer->cycle = cycle;
            timer->reset();
            timers.insert(timer);
        }

        template <typename Any> void timerStart(const Any& payload, TimerClock::duration lapse, // invokes onTimer() methods
                                                TimerCycle cycle = TimerCycle::OneShot)
        {
            Runnable* runnable = static_cast<Runnable*>(this); // safe (a dead 'this' will not dispatch timers)
            timerStart(payload, lapse, Channel<Any>([runnable](const Any& any) { runnable->onTimer(any); }), cycle);
        }

        template <typename Any> void timerReset(const Any& payload)
        {
            auto const& allTimersOfThatType = timerEvents<Any>(this);
            auto pTimer = allTimersOfThatType.find(payload);
            if (pTimer != allTimersOfThatType.end()) timerReschedule(pTimer->second.lock());
        }

        template <typename Any> void timerStop(const Any& payload)
        {
            auto& allTimersOfThatType = timerEvents<Any>(this);
            auto pTimer = allTimersOfThatType.find(payload);
            if (pTimer != allTimersOfThatType.end())
            {
                auto timer = pTimer->second.lock();
                timers.erase(timer);
                allTimersOfThatType.erase(pTimer);
                if (!timer.unique()) timer->reset(); // timer "touched" signaling to dispatcher
            }
        }

        /* the active object may throw this object while processing a message */

        struct DispatchRetry // the delivery will be retried later
        {
            DispatchRetry(TimerClock::duration waitToRetry = std::chrono::seconds(1)) : retryInterval(waitToRetry) {}
            bool operator<(const DispatchRetry&) const { return false; } // enforce a single instance in the containers
            TimerClock::duration retryInterval; // will be shortened on every incoming high priority message
        };

    private:

        struct Parcel
        {
            virtual ~Parcel() {}
            virtual void deliverTo(Runnable* instance) = 0;
        };

        template <typename Any> struct Message : public Parcel // wraps any type
        {
            Message(const Any& msg) : message(msg) {}
            Message(Any&& msg) : message(std::forward<Any>(msg)) {}
            void deliverTo(Runnable* instance) { instance->onMessage(message); }
            Any message;
        };

        template <typename Any> struct Callback : public Parcel
        {
            Callback(Channel<Any>&& msg) : message(std::forward<Channel<Any>>(msg)) {}
            void deliverTo(Runnable*) { callback<Any>() = std::move(message); }
            Channel<Any> message;
        };

        struct Timer : public Parcel, public std::enable_shared_from_this<Timer>
        {
            Timer() : deadline(TimerClock::now()) {}
            virtual ~Timer() {}
            void reset()
            {
                deadline += lapse; // try keeping regular periodic intervals
                if (deadline < TimerClock::now()) deadline = TimerClock::now() + lapse; // recover from lost events
                shoot = false;
            }
            bool operator<(const Timer& that) const // ordering in containers
            {
                if (deadline < that.deadline) return true;
                else if (that.deadline < deadline) return false;
                else return this < &that; // obviate the need for a multiset
            }
            TimerClock::duration lapse;
            TimerCycle cycle;
            TimerClock::time_point deadline;
            bool shoot;
        };

        template <typename Any> struct TimerEvent : public Timer
        {
            TimerEvent(Channel<Any>&& fn, const Any& data) : event(std::forward<Channel<Any>>(fn)), payload(data) {}
            bool operator<(const TimerEvent<Any>& that) const { return payload < that.payload; }
            void deliverTo(Runnable* instance)
            {
                this->shoot = true;
                if (event) event(payload); // the invoked function could "touch" (shoot -> false) this very same timer
                if (this->shoot)
                {
                    if (this->cycle == TimerCycle::OneShot)
                        instance->timerStop(payload);
                    else
                        instance->timerReschedule(this->shared_from_this());
                }
            }
            Channel<Any> event;
            Any payload;
        };

        void timerReschedule(const std::shared_ptr<Timer>& timer)
        {
            timers.erase(timer); // resetting will require a position change in the set nearly 100% of times
            timer->reset();
            timers.insert(timer); // emplaced in the new position
        }

        template <typename Any> static std::map<Any, std::weak_ptr<TimerEvent<Any>>>& timerEvents(ActorThread* caller)
        {
            if (caller->id != std::this_thread::get_id()) throw std::runtime_error("timer setup outside its owning thread");
            static thread_local std::map<Any, std::weak_ptr<TimerEvent<Any>>> info; // storage
            return info;
        }

        static void actorThreadRecycler(Runnable* runnable)
        {
            if (runnable->stop(true)) delete runnable; // deletion is deferred when not possible (detaching the thread)
        }

        bool stop(bool forced) // return false if couldn't be properly stop
        {
            if (runner.get_id() == std::this_thread::get_id()) // self-stop? (shared_ptr circular reference just broken)
            {
                if (forced && dispatching) // no mutex acquired (could be already locked by oneself!)
                {
                    if (runner.joinable())
                    {
                        runner.detach();
                        detached = true;
                    }
                    dispatching = false;
                }
            }
            else // normal stop invoked from another thread (or if started from run())
            {
                std::unique_lock<std::mutex> ulock(mtx);
                if (dispatching)
                {
                    dispatching = false;
                    if (runner.joinable()) // false if dispatching from run()
                    {
                        messageWaiter.notify_one();
                        ulock.unlock();
                        try { runner.join(); } catch (...) {}
                        return true;
                    }
                }
            }
            return false;
        }

        template <bool HighPri> void post(std::unique_ptr<Parcel>&& msg) // runs on the calling thread
        {
            auto& mbox = HighPri? mboxHighPri : mboxNormPri;
            std::lock_guard<std::mutex> lock(mtx);
            bool isIdle = mbox.empty();
            mbox.emplace_back(std::move(msg));
            if (HighPri) mboxPaused = false;
            if (isIdle) messageWaiter.notify_one();
        }

        void retryMbox(const DispatchRetry&) { mboxPaused = false; }

        int dispatcher() // runs on the wrapped thread
        {
            id = std::this_thread::get_id();
            Runnable* runnable = static_cast<Runnable*>(this);
            runnable->onStart();
            std::unique_lock<std::mutex> ulock(mtx);

            while (dispatching)
            {
                bool hasHigh = !mboxHighPri.empty();
                bool hasNorm = !mboxNormPri.empty();
                bool isPaused = mboxPaused;
                ulock.unlock();

                if (!isPaused && (hasHigh || hasNorm)) // consume the messages queue
                {
                    auto& mbox = hasHigh? mboxHighPri : mboxNormPri;

                    try
                    {
                        mbox.front()->deliverTo(runnable); // queue iterator valid through insertions
                        ulock.lock();
                        mbox.pop_front();
                        ulock.unlock();
                    }
                    catch (const DispatchRetry& retry)
                    {
                        auto retryTimer = Channel<DispatchRetry>([this](const DispatchRetry& dr) { this->retryMbox(dr); });
                        timerStart(retry, retry.retryInterval, retryTimer);
                        ulock.lock();
                        mboxPaused = true;
                        ulock.unlock();
                    }
                }

                for (;;) // timely handling of timers has precedence
                {
                    auto nextTimer = timers.cbegin();
                    if (nextTimer == timers.cend())
                    {
                        ulock.lock();
                        if (!(mboxNormPri.empty() && mboxHighPri.empty() && dispatching)) break;
                        idleWaiter.notify_all();
                        messageWaiter.wait(ulock); // wait for incoming messages
                        ulock.unlock();
                    }
                    else
                    {
                        auto wakeup = (*nextTimer)->deadline;
                        if (TimerClock::now() >= wakeup)
                        {
                            auto timerEvent = *nextTimer; // this shared_ptr keeps it alive when self-removed from the set
                            timerEvent->deliverTo(runnable); // here it could be self-removed (timerStop)
                        }
                        else // the other timers are scheduled even further
                        {
                            ulock.lock();
                            if (!((mboxPaused || (mboxNormPri.empty() && mboxHighPri.empty())) && dispatching)) break;
                            idleWaiter.notify_all();
                            messageWaiter.wait_until(ulock, wakeup); // wait until first timer or for incoming messages
                            ulock.unlock();
                        }
                    }
                }
            }

            ulock.unlock();
            int exitCode = runnable->onStop();
            if (detached) delete runnable; // deferred self-deletion
            return exitCode;
        }

        template <typename Key> struct PointedKeyComparator
        {
            inline bool operator()(const std::shared_ptr<Key>& key1, const std::shared_ptr<Key>& key2) const
            {
                return *key1 < *key2;
            }
        };

        bool dispatching;
        bool detached;
        std::thread runner; // also makes this class non-copyable
        std::thread::id id;
        mutable std::mutex mtx;
        std::condition_variable messageWaiter;
        std::condition_variable idleWaiter;
        std::deque<std::unique_ptr<Parcel>> mboxNormPri;
        std::deque<std::unique_ptr<Parcel>> mboxHighPri;
        bool mboxPaused;
        std::set<std::shared_ptr<Timer>, PointedKeyComparator<Timer>> timers; // ordered by deadline
};

#endif /* ACTORTHREAD_H_ */
