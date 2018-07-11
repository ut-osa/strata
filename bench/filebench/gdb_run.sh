#! /bin/bash

sudo cgdb -p `pgrep filebench | tail -n 1`
