/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef MAC_STARTUP_H
#define MAC_STARTUP_H

#include "config.h"

#include <CoreFoundation/CFDictionary.h>

#include <QString>
#include <QWidget>
#include <QKeySequence>

#ifdef __OBJC__
@class NSEvent;
#else
class NSEvent;
#endif

class PlatformInterface;
class GlobalShortcutsBackendMacOS;

namespace mac {

void MacMain();
void SetShortcutHandler(GlobalShortcutsBackendMacOS *handler);
void SetApplicationHandler(PlatformInterface *handler);

void EnableFullScreen(const QWidget &main_window);

QKeySequence KeySequenceFromNSEvent(NSEvent *event);
void DumpDictionary(CFDictionaryRef dict);

}  // namespace mac

#endif
