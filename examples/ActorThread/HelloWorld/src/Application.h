
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef APPLICATION_H
#define APPLICATION_H

#include <sys++/ActorThread.hpp>
#include "Printer.h"
#include "World.h"

struct Newspaper { std::string name; };
struct Picture   { int width, height; };
struct Money     { double amount; };

class Application : public ActorThread<Application>
{
    friend ActorThread<Application>;

    Application(int, char**);

    void onStart();
    void onMessage(Newspaper&);
    void onMessage(Picture&);
    void onMessage(Money&);
    void onTimer(const int&);
    void onStop();

    Printer::ptr printer;
    World::ptr world;
};

#endif /* APPLICATION_H */
