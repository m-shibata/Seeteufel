#! /usr/bin/env python

import os
import pygame
from pygame.locals import *
from rrb3 import *

class Seeteufel():
    def __init__(self):
        self.FPS = 60

        if not os.getenv('SDL_VIDEODRIVER'):
            os.environ["SDL_VIDEODRIVER"] = "dummy"

        try:
            pygame.display.init()
            pygame.joystic.init()
        except pygame.error:
            raise SystemExit('Failed to initialize: {}'.format(pygame.get_error())

        js_count = pygame.joystick.get_count()
        if js_count == 0:
            raise SystemExit('No joystick found')
        elif js_count > 1:
            for i in range(js_count):
                js = pygame.joystick.Joystick(i)
                print 'Joystick {0}: {1]'.format(i, js.get_name())

        events = [ JOYAXISMOTION, JOYBUTTONUP, QUIT ]
        pygame.event.set_allowed(events)

        self.js = pygame.joystick.Joystick(0)
        self.js.init()
        self.clock = pygame.time.Clock()

        try:
            self.rr = RRB3(9, 6)
        except RuntimeError:
            raise SystemExit('Failed to initialize RasPi Robot Board')

        self.rr.set_led1(0)
        self.rr.set_led2(1)

    def main_loop(self):
        done = True

        while done:
            power_on = self.rr.sw1_closed()
            if power_on:
                self.clock.tick(1)
            else
                self.clock.tick(self.FPS)

            updated_axis = False
            for event in pygame.event.get():
                if event.type == JOYAXISMOTION:
                    print 'AXIS: {}'.format(event)
                    updated_axis = True
                elif event.type == JOYBUTTONUP:
                    if event.button == 0: # square
                        self.rr.set_led1(0)
                        self.rr.stop()
                    if event.button == 1: # cross
                        self.rr.set_led1(1)
                        self.rr.stop()

                        # cancel previous events
                        updated_axis = False
                    if event.button == 2: # circle
                        self.rr.set_led1(0)
                        self.rr.stop()
                    if event.button == 3: # triangle
                        self.rr.set_led1(0)
                        self.rr.stop()
                elif event.type == QUIT:
                    self.rr.set_led1(0)
                    self.rr.set_led2(0)
                    self.rr.stop()
                    done = False

            if updated_axis:
                self.change_speed()

    def change_speed(self):
            direction = { 'left':0, 'right':0 }
            pace = { 'left':self.js.get_axis(1), 'right':self.js.get_axis(5) }
            for k,v in pace:
                if v < 0: direction[k] = 1

            self.rr.set_motors(abs(pace['left']), direction['left'],
                                abs(pace['right']), direction['right'])

if __name__ == '__main__':
    seeteufel = Seeteufel()
    seeteufel.main_loop()
