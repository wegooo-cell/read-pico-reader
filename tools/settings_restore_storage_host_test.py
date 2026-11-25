#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Actual ESP restore with failed internal storage, TF failover and version compatibility."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('settings_restore_integration_host_test.py')),run_name='__main__')
