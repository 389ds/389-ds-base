"""
   :Requirement: 389-ds-base: Command Line Utility
"""

import logging


log = logging.getLogger(__name__)

def check_value_in_log_and_reset(topology, content_list=None, content_list2=None, check_value=None,
                                 check_value_not=None):
    if content_list2 is not None:
        log.info('Check if content is present in output')
        for item in content_list + content_list2:
            assert topology.logcap.contains(item)

    if content_list is not None:
        log.info('Check if content is present in output')
        for item in content_list:
            assert topology.logcap.contains(item)

    if check_value is not None:
        log.info('Check if value is present in output')
        assert topology.logcap.contains(check_value)

    if check_value_not is not None:
        log.info('Check if value is not present in output')
        assert not topology.logcap.contains(check_value_not)

    log.info('Reset the log for next test')
    topology.logcap.flush()
