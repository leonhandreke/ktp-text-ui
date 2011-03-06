/***************************************************************************
 *   Copyright (C) 2011 by David Edmundson <kde@davidedmundson.co.uk>      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/


#include "main-window.h"

#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>


int main(int argc, char *argv[])
{
    KAboutData aboutData("telepathy-chat-window-config",
                         0,
                         ki18n("Telepathy Chat Window Config"),
                         "0.1");

    KCmdLineArgs::init(argc, argv, &aboutData);

    KApplication app;

    MainWindow w;
    w.show();

    return app.exec();
}
