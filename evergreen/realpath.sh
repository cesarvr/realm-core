#!/bin/bash

PLATFORM=$(uname -s)
case $(uname -s) in
    Darwin)
        exec perl -e 'use File::Spec; print File::Spec->rel2abs(shift); print "\n"' $1
        ;;
    *)
        exec realpath $1
        ;;
esac
