/*
 * Copyright (c) 2003 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include "smt.hh"
#include "misc.hh"

#include "eventq.hh"
#include "trace.hh"
#include "universe.hh"

using namespace std;

const string Event::defaultName("event");

//
// Main Event Queue
//
// Events on this queue are processed at the *beginning* of each
// cycle, before the pipeline simulation is performed.
//
EventQueue mainEventQueue("Main Event Queue");

void
EventQueue::insert(Event *event)
{
    if (head == NULL || event->when() < head->when() ||
        (event->when() == head->when() &&
         event->priority() <= head->priority())) {
        event->next = head;
        head = event;
    } else {
        Event *prev = head;
        Event *curr = head->next;

        while (curr) {
            if (event->when() <= curr->when() &&
                (event->when() < curr->when() ||
                 event->priority() <= curr->priority()))
                break;

            prev = curr;
            curr = curr->next;
        }

        event->next = curr;
        prev->next = event;
    }
}

void
EventQueue::remove(Event *event)
{
    if (head == NULL)
        return;

    if (head == event){
        head = event->next;
        return;
    }

    Event *prev = head;
    Event *curr = head->next;
    while (curr && curr != event) {
        prev = curr;
        curr = curr->next;
    }

    if (curr == event)
        prev->next = curr->next;
}

void
EventQueue::serviceOne()
{
    Event *event = head;
    event->clearFlags(Event::Scheduled);
    head = event->next;

    // handle action
    if (!event->squashed())
        event->process();
    else
        event->clearFlags(Event::Squashed);

    if (event->getFlags(Event::AutoDelete))
        delete event;
}

void
EventQueue::nameChildren()
{
    int j = 0;

    Event *event = head;
    while (event) {
        stringstream stream;
        ccprintf(stream, "%s.event%d", name(), j++);
        event->setName(stream.str());

        event = event->next;
    }
}

void
EventQueue::serialize()
{
    string objects = "";

    Event *event = head;
    while (event) {
        objects += event->name();
        objects += " ";
        event->serialize();

        event = event->next;
    }
    nameOut("Serialized");
    paramOut("objects",objects);
}

void
EventQueue::dump()
{
    cprintf("============================================================\n");
    cprintf("EventQueue Dump  (cycle %d)\n", curTick);
    cprintf("------------------------------------------------------------\n");

    if (empty())
        cprintf("<No Events>\n");
    else {
        Event *event = head;
        while (event) {
            event->dump();
            event = event->next;
        }
    }

    cprintf("============================================================\n");
}


const char *
Event::description()
{
    return "generic";
}

#if TRACING_ON
void
Event::trace(const char *action)
{
    // This DPRINTF is unconditional because calls to this function
    // are protected by an 'if (DTRACE(Event))' in the inlined Event
    // methods.
    //
    // This is just a default implementation for derived classes where
    // it's not worth doing anything special.  If you want to put a
    // more informative message in the trace, override this method on
    // the particular subclass where you have the information that
    // needs to be printed.
    DPRINTFN("%s event %s @ %d\n", description(), action, when());
}
#endif

void
Event::dump()
{
#if TRACING_ON
    cprintf("   Created: %d\n", when_created);
#endif
    if (scheduled()) {
#if TRACING_ON
        cprintf("   Scheduled at  %d\n", when_scheduled);
#endif
        cprintf("   Scheduled for %d\n", when());
    }
    else {
        cprintf("   Not Scheduled\n");
    }
}
