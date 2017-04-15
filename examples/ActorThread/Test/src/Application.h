
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef APPLICATION_H
#define APPLICATION_H

#include <random>
#include <set>
#include <sys++/ActorThread.hpp>

struct SyncBegin { bool master; };
struct SyncMsg { int counter; };
struct SyncEnd { int counter; };

struct AsyncBegin {};
struct AsyncMsg { int counter; bool last; };
struct AsyncEnd { int counter; };

struct MixedBegin {};
struct A {};
struct B {};
struct MixedEnd {};
struct MixedStats { int sntA, sntB, recvA, recvB; };

struct MpscBegin { int id; };
struct Mpsc { int id; int counter; };
struct MpscEnd { int id; };

struct BreedExplode { int amount; int generation; int maxGenerations; };
struct BreedImplode { std::shared_ptr<class Task> child; int implosions; };

class Task : public ActorThread<Task>
{
    friend ActorThread<Task>;

    Task(std::shared_ptr<class Application> parent)
      : app(parent), gen(std::random_device{}()), rnd(0,9),
        syncTestCompleted(false), mixedTestCompleted(false), mixedTestPaused(false),
        fstats { 0, 0, 0, 0 }, implosions(0) {}

    Task(Task::ptr parent) : ancestor(parent), implosions(0) {} // for breeding test

    template <typename Any> void onMessage(Any&);
    template <typename Any> void onTimer(const Any&);
    void doMixed();

    std::shared_ptr<class Application> app;
    Task::ptr sibling;

    std::default_random_engine gen;
    std::uniform_int_distribution<int> rnd;

    bool syncTestCompleted;
    bool mixedTestCompleted;
    bool mixedTestPaused;

    MixedStats fstats;

    Task::ptr ancestor;
    std::set<Task::ptr> pendingChilds;
    int implosions;
};

class Application : public ActorThread<Application>
{
    friend ActorThread<Application>;

    Application(int cmdArgc, char** cmdArgv) : argc(cmdArgc), argv(cmdArgv) {}

    void onStart();

    template <typename Any> void onMessage(Any&);
    template <typename Any> void onTimer(const Any&);

    const int argc;
    char** const argv;

    Task::ptr snd1;
    Task::ptr snd2;

    std::chrono::steady_clock::time_point tStart;
    int repliesCount;

    int count_mpsc1, count_mpsc2, count_mpsc1_lap, count_mpsc2_lap;
    double mpsc_elapsed_lap, mpsc_elapsed_sc1, mpsc_elapsed_sc2;
    bool crazyScheduler;
};

#endif /* APPLICATION_H */
