// Active Object pattern wrapping a standard C++11 thread (https://github.com/lightful/syscpp)
//
//       Copyright Ciriaco Garcia de Celis 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)
/*
 - Publicly inherit from this template (specializing it for the derived class itself)
 - On the derived (active object) class everything should be private: make this base a friend
 - Instance the active object by invoking the inherited public static create() or run() methods
 - Use send() to send or move messages (of any data type) to the active object
 - Use onMessage(AnyType&) methods to implement the messages reception on the active object
 - Optionally use a Gateway wrapper or build Channel objects instead of send()
 - Optionally override onStart() and onStop() in the active object
 - Optionally use connect() from unknown clients to bind callbacks for any data type
 - Optionally use publish() from the active object to invoke the binded callbacks
 - Optionally use timerStart() / timerStop() / timerReset() from the active object
 */
#ifndef ACTORTHREAD_HPP
#define ACTORTHREAD_HPP

#include <memory>
#include <functional>
#include <utility>
#include <cstddef>
#include <type_traits>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <set>
#include <map>

template <typename Runnable> class ActorThread
{
    public:

        typedef std::shared_ptr<Runnable> ptr;

        std::weak_ptr<Runnable> weak_from_this() const noexcept { return weak_this; } // shared_from_this() would be unsafe

        template <typename ... Args> static ptr create(Args&&... args) // spawn a new thread
        {
            auto task = ptr(new Runnable(std::forward<Args>(args)...), actorThreadRecycler);
            task->weak_this = task;
            task->runner = std::thread(&ActorThread::dispatcher, task.get());
            return task;
        }

        template <typename ... Args> static int run(Args&&... args) // run in the calling thread (e.g. main() thread)
        {
            struct ActRunTask : public Runnable { ActRunTask(Args&&... arg) : Runnable(std::forward<Args>(arg)...) {} };
            auto task = std::make_shared<ActRunTask>(std::forward<Args>(args)...);
            task->weak_this = task;
            return task->dispatcher();
        }

        template <bool HighPri = false, typename Any> inline void send(Any msg) // polymorphic message passing
        {
            post<ActorMessage<Any>, HighPri>(std::move(msg)); // gratis rvalue onwards
        }

        template <typename Any> using Channel = std::function<void(Any&)>;

        template <typename Any, bool HighPri = false> Channel<Any> getChannel() const // build a generic movement callback
        {
            std::weak_ptr<ActorThread> weakBind(this->weak_from_this()); // Note:
            return Channel<Any>([weakBind](Any& data)                    // std::bind can't store a weak_ptr
            {                                                            // in std::function (and a shared_ptr
                auto aliveTarget = weakBind.lock();                      // would prevent objects destruction)
                if (aliveTarget)
                    std::static_pointer_cast<Runnable>(aliveTarget)->template send<HighPri>(std::move(data));
            });
        }

        template <typename Any> void connect(Channel<Any> receiver = Channel<Any>()) // bind (or unbind) a generic callback
        {
            post<ActorCallback<Any>, true>(std::move(receiver));
        }

        template <typename Any, bool HighPri = false, typename Him> void connect(const std::weak_ptr<Him>& receiver)
        {
            auto aliveTarget = receiver.lock();
            if (aliveTarget) connect(aliveTarget->template getChannel<Any, HighPri>()); // bind another ActorThread
        }

        std::size_t pendingMessages() const // amount of undispatched messages in the active object
        {
            return mboxNormPri.size() + mboxHighPri.size();
        }

        typedef std::chrono::steady_clock TimerClock;

        void waitIdle(TimerClock::duration maxWait = std::chrono::seconds(1)) // blocks until there aren't pending messages
        {
            std::unique_lock<std::mutex> ulock(mtx);
            if (!(mboxNormPri.empty() && mboxHighPri.empty())) idleWaiter.wait_until(ulock, TimerClock::now() + maxWait);
        }

        void stop(int code = 0) // optional call from ANOTHER thread (suffices deleting the object) or if created from run()
        {
            if (stop(false)) exitCode = code; // return code for run() function
        }

        bool exiting() const // mostly to allow active objects running intensive jobs to poll for a shutdown request
        {
            return !dispatching;
        }

        struct Gateway // safe wrapper for instances of unknown lifecycle
        {
            Gateway(const std::weak_ptr<Runnable>& actorThread = ptr()) : actor(actorThread) {}

            template <typename Any> inline void operator()(Any&& msg) const // handy function-like syntax
            {
                auto aliveTarget = get();
                if (aliveTarget) aliveTarget->template send<false>(std::forward<Any>(msg));
            }

            void set(const std::weak_ptr<Runnable>& actorThread) { actor = actorThread; }

            inline ptr get() const { return actor.lock(); }

            private: std::weak_ptr<Runnable> actor;
        };

    protected:

        ActorThread() : dispatching(true), externalDispatcher(false), detached(false), exitCode(0), mboxPaused(false) {}

        virtual ~ActorThread() {} // messages pending to be dispatched are discarded

        /* methods invoked on the active object (this default implementation can be "overrided") */

        void onStart() {}
        void onStop() {}

        std::thread::id threadID() const { return id; }

        /* the active object may use this family of methods to perform the callbacks onto connected clients */

        template <typename Any> inline static void publish(Any msg)
        {
            auto& bearer = callback<Any>();
            if (bearer) bearer(msg); // if binded with getChannel() won't call a deleted peer
        }

        template <typename Any> static Channel<Any>& callback() // callback storage (per-thread and type)
        {
            static thread_local Channel<Any> bearer; // beware that this callback moves the argument
            return bearer;
        }

        /* timers facility for the active object (unlimited amount: one per each "payload" instance) */

        enum class TimerCycle { Periodic, OneShot };

        template <typename Any> void timerStart(const Any& payload, TimerClock::duration lapse,
                                                Channel<const Any> event, TimerCycle cycle = TimerCycle::OneShot)
        {
            std::shared_ptr<ActorAlarm<Any>> timer;
            auto& allTimersOfThatType = timerEvents<Any>(this);
            auto pTimer = allTimersOfThatType.find(payload);
            if (pTimer == allTimersOfThatType.end())
            {
                timer = std::make_shared<ActorAlarm<Any>>(std::move(event), payload);
                allTimersOfThatType.emplace(timer->payload, timer);
            }
            else // reschedule and reprogram
            {
                timer = pTimer->second.lock();
                timers.erase(timer);
                timer->event = std::move(event);
            }
            timer->lapse = lapse;
            timer->cycle = cycle;
            timer->reset(false);
            timers.insert(std::move(timer));
        }

        template <typename Any> void timerStart(const Any& payload, TimerClock::duration lapse, // invokes onTimer() methods
                                                TimerCycle cycle = TimerCycle::OneShot)
        {
            Runnable* runnable = static_cast<Runnable*>(this); // safe (a dead 'this' will not dispatch timers)
            timerStart(payload, lapse, Channel<const Any>([runnable](const Any& p) { runnable->onTimer(p); }), cycle);
        }

        template <typename Any> void timerReset(const Any& payload)
        {
            auto const& allTimersOfThatType = timerEvents<Any>(this);
            auto pTimer = allTimersOfThatType.find(payload);
            if (pTimer != allTimersOfThatType.end()) timerReschedule(pTimer->second.lock(), false);
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
                if (timer.use_count() > 1) timer->reset(false); // timer "touched" signaling to dispatcher
            }
        }

        /* the active object may throw this object while processing a message */

        struct DispatchRetry // the delivery will be retried later
        {
            DispatchRetry(TimerClock::duration waitToRetry = std::chrono::seconds(1)) : retryInterval(waitToRetry) {}
            bool operator<(const DispatchRetry&) const { return false; } // enforce a single instance in the containers
            TimerClock::duration retryInterval; // will be shortened on every incoming high priority message
        };

        // The following methods are exclusively intended to *interleave* the ActorThread dispatcher with another
        // external dispatcher (e.g. Asio) which will actually be the master dispatcher having the thread control:
        //
        // - onWaitingEvents() must be used to request the external dispatcher to invoke handleActorEvents()
        // - onWaitingTimer() must request the external dispatcher a delayed invocation of handleActorEvents()
        // - onWaitingTimerCancel() must request the external dispatcher to cancel any delayed invocation
        // - onStopping() must request the external dispatcher to end (not required a synchronous completion wait)

        void acquireDispatcher() // request ActorThread to stop dispatching and invoke onDispatching()
        {
            std::lock_guard<std::mutex> lock(mtx);
            externalDispatcher = true;
            messageWaiter.notify_one();
        }

        void onDispatching() {} // run the external dispatcher from here (in case it exits, ActorThread resumes again)

        void onWaitingEvents() {} // invoked from another threads (new messages coming)
        void onWaitingTimer(TimerClock::duration); // invoked from dispatcher thread (there is only a single timer)
        void onWaitingTimerCancel(); // invoked from dispatcher thread (will come even if the timer was not in use)
        void onStopping() {} // invoked from another threads (mandatory handling: the object could be about to be deleted)

        void handleActorEvents() // this must be invoked from the external dispatcher as specified above
        {
            auto status = eventsLoop();
            if (status.first)
                static_cast<Runnable*>(this)->onWaitingTimer(status.second);
            else
                static_cast<Runnable*>(this)->onWaitingTimerCancel();
        }

    private:

        ActorThread& operator=(const ActorThread&) = delete;
        ActorThread(const ActorThread&) = delete;

        template <typename Item> class ActorQueue // FIFO (based on the MPSC queue at https://github.com/mstump/queues)
        {
            template <typename T> using Aligned = typename std::aligned_storage<sizeof(T),std::alignment_of<T>::value>::type;

            public:

                struct Linked { std::atomic<Item*> next; };

                ActorQueue() : head(reinterpret_cast<Item*>(new Aligned<Item>)), // dummy transient placeholder
                               tail(head.load(std::memory_order_relaxed)),
                               count(0),
                               lastFront(nullptr),
                               prevFront(tail.load(std::memory_order_relaxed))
                {
                    prevFront->next.store(nullptr, std::memory_order_relaxed);
                }

                void clear() { while (front()) pop_front(); }

                ~ActorQueue()
                {
                    clear();
                    ::operator delete(head.load(std::memory_order_relaxed)); // also suitable for a dummy instance
                }

                template <typename Linkable> std::size_t push_back(Linkable* item) // e.g. any type derived from 'Linked'
                {
                    item->next.store(nullptr, std::memory_order_relaxed);
                    Item* back = head.exchange(item, std::memory_order_acq_rel);
                    back->next.store(item, std::memory_order_release);
                    return count.fetch_add(1, std::memory_order_release); // amount of queued items minus 1
                }

                inline Item* front() // returns nullptr if empty (must be always invoked just before pop_front())
                {
                    return lastFront = prevFront->next.load(std::memory_order_acquire);
                }

                void pop_front() // also destroys and ultimately deletes the item
                {
                    count.fetch_sub(1, std::memory_order_release);
                    tail.store(lastFront, std::memory_order_release);
                    lastFront->~Item(); // (atomic destructor is trivial) memory deletion is actually deferred one step behind
                    ::operator delete(prevFront); // delete the *previous* item memory no longer needed
                    prevFront = lastFront;
                }

                inline std::size_t size() const { return count.load(std::memory_order_acquire); }

                inline bool empty() const { return !size(); }

            private:

                std::atomic<Item*> head; // the stored objects hold the linked list pointers (single memory allocation)
                std::atomic<Item*> tail;
                std::atomic<std::size_t> count;
                Item* lastFront;
                Item* prevFront;
        };

    protected:

        struct ActorParcel : public ActorQueue<ActorParcel>::Linked
        {
            virtual ~ActorParcel() {}
            virtual void deliverTo(Runnable* instance) = 0;
        };

    private:

        template <typename Any> struct ActorMessage : public ActorParcel // wraps any type
        {
            ActorMessage(Any&& msg) : message(std::move(msg)) {}
            void deliverTo(Runnable* instance) { instance->onMessage(message); }
            Any message;
        };

        template <typename Any> struct ActorCallback : public ActorParcel
        {
            ActorCallback(Channel<Any>&& msg) : message(std::move(msg)) {}
            void deliverTo(Runnable*) { callback<Any>() = std::move(message); }
            Channel<Any> message;
        };

        struct ActorTimer : public ActorParcel, public std::enable_shared_from_this<ActorTimer>
        {
            virtual ~ActorTimer() {}
            void reset(bool incremental)
            {
                if (!incremental) deadline = TimerClock::now();
                deadline += lapse; // try keeping regular periodic intervals
                if (incremental && (deadline < TimerClock::now())) deadline = TimerClock::now() + lapse; // fix lost events
                shoot = false;
            }
            bool operator<(const ActorTimer& that) const // ordering in containers
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

        template <typename Any> struct ActorAlarm : public ActorTimer
        {
            ActorAlarm(Channel<const Any>&& fn, const Any& p) : event(std::move(fn)), payload(p) {}
            void deliverTo(Runnable* instance)
            {
                this->shoot = true;
                if (event) event(payload); // the invoked function could "touch" (shoot -> false) this very same timer
                if (this->shoot)
                {
                    if (this->cycle == TimerCycle::OneShot)
                        instance->timerStop(payload);
                    else
                        instance->timerReschedule(this->shared_from_this(), true);
                }
            }
            Channel<const Any> event;
            Any payload;
        };

        void timerReschedule(std::shared_ptr<ActorTimer>&& timer, bool incremental)
        {
            timers.erase(timer); // resetting will require a position change in the set nearly 100% of times
            timer->reset(incremental);
            timers.insert(std::move(timer)); // emplaced in the new position
        }

        template <typename Any> static std::map<Any, std::weak_ptr<ActorAlarm<Any>>>& timerEvents(ActorThread* caller)
        {
            if (caller->id != std::this_thread::get_id()) throw std::runtime_error("timer setup outside its owning thread");
            static thread_local std::map<Any, std::weak_ptr<ActorAlarm<Any>>> info; // storage
            return info;
        }

        static void actorThreadRecycler(Runnable* runnable)
        {
            if (runnable->stop(true)) delete runnable; // deletion is deferred when not possible (detaching the thread)
        }

        bool stop(bool forced) try // return false if couldn't be properly stop
        {
            std::unique_lock<std::mutex> ulock(mtx);
            if (runner.get_id() == std::this_thread::get_id()) // self-stop?
            {
                if (forced && dispatching) // from delete? (shared_ptr circular reference just broken)
                {
                    if (runner.joinable())
                    {
                        runner.detach();
                        detached = true;
                    }
                    dispatching = false;
                    ulock.unlock();
                    static_cast<Runnable*>(this)->onStopping();
                }
                return false;
            }
            else // normal stop invoked from another thread (or if started from run())
            {
                if (!dispatching) return true; // was already stop
                dispatching = false;
                bool fromCreate = runner.joinable();
                if (fromCreate) messageWaiter.notify_one();
                ulock.unlock();
                static_cast<Runnable*>(this)->onStopping();
                if (!fromCreate) return true; // queues don't require and can't be cleared (potentially inside onMessage())
                runner.join();
                timers.clear();
                mboxNormPri.clear(); // don't wait for this object deletion (the frozen queues
                mboxHighPri.clear(); // may store shared_ptr preventing other objects deletion)
                return true;
            }
        }
        catch (...) { return false; }

    protected:

        template <typename Parcelable, bool HighPri, typename Any> void post(Any&& msg) // runs on the calling thread
        {
            auto& mbox = HighPri? mboxHighPri : mboxNormPri;
            if (!dispatching) return; // don't store anything in a frozen queue
            bool isIdle = mbox.push_back(new Parcelable(std::forward<Any>(msg))) == 0;
            if (HighPri) mboxPaused = false;
            if (!isIdle) return;
            std::lock_guard<std::mutex> lock(mtx);
            messageWaiter.notify_one();
            static_cast<Runnable*>(this)->onWaitingEvents();
        }

    private:

        int dispatcher() // runs on the wrapped thread
        {
            id = std::this_thread::get_id();
            Runnable* runnable = static_cast<Runnable*>(this);
            runnable->onStart();
            for (;;)
            {
                burst = 0;
                eventsLoop();
                if (!dispatching) break;
                runnable->onDispatching();
                externalDispatcher = false;
            }
            runnable->onStop();
            int code = exitCode;
            if (detached) delete runnable; // deferred self-deletion
            return code;
        }

        void retryMbox(const DispatchRetry&) { mboxPaused = false; }

        std::pair<bool, TimerClock::duration> eventsLoop()
        {
            bool haveTimerLapse = false;
            TimerClock::duration timerLapse;
            Runnable* runnable = static_cast<Runnable*>(this);
            bool mustDispatch = true;
            while (dispatching && mustDispatch)
            {
                bool hasHigh = !mboxHighPri.empty();
                bool hasNorm = !mboxNormPri.empty();

                if (!mboxPaused && (hasHigh || hasNorm)) // consume the messages queue
                {
                    auto& mbox = hasHigh? mboxHighPri : mboxNormPri;
                    try
                    {
                        while (ActorParcel* msg = mbox.front())
                        {
                            msg->deliverTo(runnable);
                            mbox.pop_front();
                            if ((++burst % 64) == 0)
                            {
                                if (externalDispatcher) // do not monopolize the CPU on this dispatcher
                                {
                                    runnable->onWaitingEvents(); // queue a resume request
                                    mustDispatch = false;
                                }
                                break; // keep an eye on the timers
                            }
                        }
                    }
                    catch (const DispatchRetry& retry)
                    {
                        auto event = Channel<const DispatchRetry>([this](const DispatchRetry& dr) { retryMbox(dr); });
                        timerStart(retry, retry.retryInterval, std::move(event));
                        mboxPaused = true;
                    }
                }

                auto firstTimer = timers.cbegin();
                if (firstTimer == timers.cend())
                {
                    std::unique_lock<std::mutex> ulock(mtx);
                    if (mboxNormPri.empty() && mboxHighPri.empty() && dispatching)
                    {
                        idleWaiter.notify_all();
                        if (externalDispatcher) break;
                        messageWaiter.wait(ulock); // wait for incoming messages
                    }
                }
                else
                {
                    auto wakeup = (*firstTimer)->deadline;
                    if (TimerClock::now() >= wakeup)
                    {
                        auto timerEvent = *firstTimer; // this shared_ptr keeps it alive when self-removed from the set
                        timerEvent->deliverTo(runnable); // here it could be self-removed (timerStop)
                    }
                    else // the other timers are scheduled even further
                    {
                        std::unique_lock<std::mutex> ulock(mtx);
                        if (dispatching && mboxHighPri.empty() && (mboxNormPri.empty() || mboxPaused))
                        {
                            idleWaiter.notify_all();
                            if (externalDispatcher)
                            {
                                haveTimerLapse = true;
                                timerLapse = wakeup - TimerClock::now();
                                break;
                            }
                            messageWaiter.wait_until(ulock, wakeup); // wait until first timer or for incoming messages
                        }
                    }
                }
            }
            return std::make_pair(haveTimerLapse, timerLapse);
        }

        template <typename Key> struct ActorPointedKeyComparator
        {
            inline bool operator()(const std::shared_ptr<Key>& key1, const std::shared_ptr<Key>& key2) const
            {
                return *key1 < *key2;
            }
        };

        std::atomic<bool> dispatching;
        std::atomic<bool> externalDispatcher;
        std::atomic<bool> detached;
        mutable std::weak_ptr<Runnable> weak_this;
        std::thread runner;
        std::thread::id id;
        int exitCode;
        mutable std::mutex mtx;
        std::condition_variable messageWaiter;
        std::condition_variable idleWaiter;
        ActorQueue<ActorParcel> mboxNormPri;
        ActorQueue<ActorParcel> mboxHighPri;
        std::atomic<bool> mboxPaused;
        uint16_t burst;
        std::set<std::shared_ptr<ActorTimer>, ActorPointedKeyComparator<ActorTimer>> timers; // ordered by deadline
};

#endif /* ACTORTHREAD_HPP */
