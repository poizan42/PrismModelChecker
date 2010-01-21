#!/bin/sh

# full (multi-variable)

prism zeroconf.nm incorrect.pctl -aroptions refine=all,nopre,opt

prism zeroconf.nm deadline.pctl -const T=100 -aroptions refine=all,nopre,opt
prism zeroconf.nm deadline.pctl -const T=150 -aroptions refine=all,nopre,opt
prism zeroconf.nm deadline.pctl -const T=200 -aroptions refine=all,nopre,opt

prism zeroconf.nm time.pctl -aroptions refine=all,nopre,opt

# simple (single variable)

prism zeroconf-simple.nm incorrect.pctl -aroptions refine=all,nopre,opt

prism zeroconf-simple.nm deadline.pctl -const T=100 -aroptions refine=all,nopre,opt
prism zeroconf-simple.nm deadline.pctl -const T=150 -aroptions refine=all,nopre,opt
prism zeroconf-simple.nm deadline.pctl -const T=200 -aroptions refine=all,nopre,opt

