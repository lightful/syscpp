
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef WORLD_H
#define WORLD_H

#include <string>
#include <sys++/ActorThread.hpp>
#include "Printer.h"

struct Kiosk   { std::string itemRequest; };
struct Gallery { std::string pictureName; std::string author; };
struct Bank    { double amount; std::string account; };

class World : public ActorThread<World>
{
    friend ActorThread<World>;

    World(const std::shared_ptr<class Application>& myCreator) : app(myCreator) {}

    void onMessage(Printer::ptr&);
    void onMessage(int);
    void onMessage(Kiosk&);
    void onMessage(Gallery&);
    void onMessage(Bank&);

    Printer::ptr printer;
    std::shared_ptr<class Application> app; // equivalent to ActorThread<class Application>::ptr
};

#endif /* WORLD_H */
