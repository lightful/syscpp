
//         Copyright Ciriaco Garcia de Celis 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <iostream>
#include "World.h"
#include "Application.h"

void World::onMessage(Printer::ptr& prn)
{
    printer = prn;
    printer->send(LINE("<world> now I can also print!"));
}

void World::onMessage(int year)
{
    printer->send(LINE("<world> year " << year));
}

void World::onMessage(Kiosk& msg)
{
    printer->send(LINE("<world> is requested: " << msg.itemRequest));
    app->send(Newspaper { "The Times" });
}

void World::onMessage(Gallery& msg)
{
    printer->send(LINE("<world> is requested: " << msg.pictureName << " (" << msg.author << ")"));
    app->send(Picture { 1024, 768 });
}

void World::onMessage(Bank& msg)
{
    printer->send(LINE("<world> is requested: " << msg.amount << " euros from " << msg.account));
    app->send(Money { msg.amount });
}
