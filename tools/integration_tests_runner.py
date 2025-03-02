from __future__ import print_function

import logging
import multiprocessing
from optparse import OptionParser
from os import path
import shutil
import subprocess
import tempfile
from threading import Lock
from threading import Thread
import traceback


from Queue import Queue


from run_desktop_tests import tests_on_disk

__author__ = 't.danshin'


TEMPFOLDER_TESTS = ["search_integration_tests"]


class IntegrationRunner:
    def __init__(self):
        self.process_cli()

        self.proc_count = multiprocessing.cpu_count()
        logging.info("Number of processors is: {nproc}".format(nproc=self.proc_count))

        self.file_lock = Lock()
        self.tests = Queue()


    def run_tests(self):
        for exec_file in self.runlist:
            tests = self.get_tests_from_exec_file(exec_file, "--list_tests")[0]
            for test in tests:
                self.tests.put((exec_file, test))

        self.file = open(self.output, "w")
        self.run_parallel_tests()
        self.file.close()


    def run_parallel_tests(self):
        threads = list()

        for i in range(0, self.proc_count):
            thread = Thread(target=self.exec_tests_in_queue)
            thread.start()
            threads.append(thread)

        for thread in threads:
            thread.join()


    def exec_tests_in_queue(self):
        while True:
            try:
                if self.tests.empty():
                    return

                test_file, test = self.tests.get()
                self.exec_test(test_file, test, clean_env=(test_file in TEMPFOLDER_TESTS))

            except:
                logging.error(traceback.format_exc())
                return


    def exec_test(self, test_file, test, clean_env=False):
        keys = '"--filter={test}"'.format(test=test)
        if clean_env:
            tmpdir = tempfile.mkdtemp()
            keys = '{old_key} "--user_resource_path={tmpdir}"'.format(old_key=keys, tmpdir=tmpdir)
            logging.debug("Temp dir: {tmpdir}".format(tmpdir=tmpdir))

        out, err, result = self.get_tests_from_exec_file(test_file, keys)

        if clean_env:
            try:
                shutil.rmtree(tmpdir)
            except:
                logging.error("Failed to remove tempdir {tmpdir}".format(tmpdir=tmpdir))

        with self.file_lock:
            self.file.write("BEGIN: {file}\n".format(file=test_file))
            self.file.write(str(err))
            self.file.write("\nEND: {file} | result: {res}\n\n".format(file=test_file, res=result))
            self.file.flush()


    def get_tests_from_exec_file(self, test, keys):
        spell = "{test} {keys}".format(test=path.join(self.workspace_path, test), keys=keys)
        logging.debug(">> {spell}".format(spell=spell))

        process = subprocess.Popen(spell.split(" "),
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE
                                   )

        out, err = process.communicate()
        result = process.returncode

        return filter(lambda x: x,  out.split("\n")), err, result


    def process_cli(self):
        parser = OptionParser()
        parser.add_option("-o", "--output", dest="output", default="testlog.log", help="resulting log file. Default testlog.log")
        parser.add_option("-f", "--folder", dest="folder", default="omim-build-release/out/release", help="specify the folder where the tests reside (absolute path or relative to the location of this script)")
        parser.add_option("-i", "--include", dest="runlist", action="append", default=[], help="Include test into execution, comma separated list with no spaces or individual tests, or both. E.g.: -i one -i two -i three,four,five")

        (options, args) = parser.parse_args()

        if not options.runlist:
            logging.warn("You must provide the list of tests to run. This runner doesn't run all the tests it finds, only the ones you specify.")
            exit(2)

        self.workspace_path = options.folder
        interim_runlist = list()
        for opt in options.runlist:
            interim_runlist.extend(map(lambda x: x.strip(), opt.split(",")))

        self.runlist = filter(lambda x: x in tests_on_disk(self.workspace_path), interim_runlist)
        self.output = options.output


if __name__ == "__main__":
    runner = IntegrationRunner()
    runner.run_tests()
