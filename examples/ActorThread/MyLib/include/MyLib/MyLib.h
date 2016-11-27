
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef MYLIB_H
#define MYLIB_H

#include <string>
#include <atomic>
#include <sys++/ActorThread.hpp>
#include "Printer.h"

struct WantPrinter {};

struct LibraryIsTired
{
    bool operator<(const LibraryIsTired&) const { return false; } // ordering is required for timers
};

struct RequestA { std::string data; RequestA(const std::string& s) : data(s) {} };
struct RequestB { std::string data; RequestB(const std::string& s) : data(s) {} };
struct ReplyA   { std::string data; ReplyA(const std::string& s)   : data(s) {} };
struct ReplyB   { std::string data; ReplyB(const std::string& s)   : data(s) {} };
struct Info     { std::string data; Info(const std::string& s)     : data(s) {} };

struct Billing
{
    std::atomic<int> count; // shared message (same raw pointer accessed by two threads)
};

class MyLib : public ActorThread<MyLib>
{
    friend ActorThread<MyLib>;

    MyLib() : printer(Printer::create()), bills(std::make_shared<Billing>()) {}

    template <typename Any> void onMessage(Any&);
    template <typename Any> void onTimer(const Any&);

    Printer::ptr printer;
    std::shared_ptr<Billing> bills;

  public:

    template <typename Client> void basicSubscriptions(const Client& client) // basic data reception by clients
    {
        connect<Printer::ptr>(client);
        connect<LibraryIsTired>(client);
        connect<std::shared_ptr<Info>>(client);
        connect<std::shared_ptr<Billing>>(client);
    }
};

#endif /* MYLIB_H */
