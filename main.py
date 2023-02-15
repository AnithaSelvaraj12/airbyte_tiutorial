#
# Copyright (c) 2023 Airbyte, Inc., all rights reserved.
#


import sys

from airbyte_cdk.entrypoint import launch
from source_tutorial_example import SourceTutorialExample

if __name__ == "__main__":
    source = SourceTutorialExample()
    launch(source, sys.argv[1:])
