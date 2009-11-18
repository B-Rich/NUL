#!/usr/bin/env python
"""Hectic is an event based job system."""

class State:
    def __init__(self):
        self.values = {}
    def get(self, default, *names):
        "Get a value by with optional names. At least one name must be defined"
        for x in range((1 << len(names)) - 1):
            s = []
            for i in range(len(names)):
                s.append(~x & (1 << i) and names[i] or "")
            st = ":".join(s)
            if self.values.has_key(st):  return self.values[st]
        return default
    def __setitem__(self, key, value): self.values[key] = value
    def setdefault(self, key, value): self.values.setdefault(key, value)

def debug(postfix, name, *params):
    "output a debug string if a debug.postfix.name config option is present"
    if config.get(None, "debug", postfix, name): print "%s: '%s'"%(postfix, name), " ".join(map(str,params))

class Job:
    "Jobs have a name and can have dependencies."
    def __init__(self, name, **params):
        self.name = name
        self.rdeps = []
        self.wait  = 0
        self.__dict__.update(params)
    def add_deps(self, q, deps):
        self.wait += len(deps)
        for s in deps:
            s.rdeps.append(self)
            if not s.wait:
                q.put(s)
    def execute(self, q):
        "execute this event, new events can be generated and be put into the workqueue"
        # wakeup Nodes depending on ourself
        debug("execute", "executed", self, self.rdeps)
        for r in self.rdeps:
            assert r.wait,(r.wait, self)
            r.wait -= 1
            if r.wait == 0:
                q.put(r)

class Hectic:
    """Hectic consists of a couple worker threads waiting on a queue for
    jobs to execute."""
    def __init__(self):
        import Queue
        self.q = Queue.Queue()
    def worker(self):
        while True:
            try:
                job = self.q.get()
                debug("===", job.name)
                job.execute(self)
            except:
                import traceback
                traceback.print_exc()
            debug("<<<", job.name)
            self.q.task_done()
    def put(self, job):
        "put something in the work queue"
        debug(">>>", job.name)
        self.q.put(job)
    def run(self):
        "run and wait for all jobs to finish"
        import threading
        for i in range(config.get(1, "hectic:threads")):
            t = threading.Thread(target=self.worker)
            t.setDaemon(True)
            t.start()
        self.q.join()


config = State()
if __name__ == "__main__":
    import sys, os

    # default parameters
    config["hectic:threads"] = 8
    config["hectic:configfile"] = "Hectic"
    h = Hectic()

    # evaluate cmdline params
    for arg in sys.argv[1:]:
        key,value = arg, None
        try:
            key, value = arg.split("=")
            value = int(value)
        except ValueError:
            pass
        config[key] = value

    # config file
    exec open(config.get("", "hectic:configfile"))

    # run the jobs
    h.run()
