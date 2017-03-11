---
title: Related Tools
layout: default
---

# Related tools

There are various tools whose functionality overlaps or is closely related with that of Storm (in lexicographic order):

- [COMICS](http://www-i2.informatik.rwth-aachen.de/i2/comics/){:target="_blank"}
- [DFTCalc](http://fmt.ewi.utwente.nl/tools/dftcalc/){:target="_blank"}
- [FACT](https://www-users.cs.york.ac.uk/~cap/FACT/){:target="_blank"}
- FIG[^1]
- [GreatSPN](http://www.di.unito.it/~greatspn/index.html){:target="_blank"}
- [IMCA](http://www-i2.informatik.rwth-aachen.de/imca/index.html){:target="_blank"}
- [IscasMC](http://iscasmc.ios.ac.cn/){:target="_blank"}
- [Modest](http://modestchecker.org/){:target="_blank"}
- [MRMC](http://mrmc-tool.org/){:target="_blank"}
- [PARAM](http://depend.cs.uni-sb.de/tools/param/){:target="_blank"}
- [PASS](https://depend.cs.uni-sb.de/tools/pass/){:target="_blank"}
- [PAT](http://www.comp.nus.edu.sg/~pat/){:target="_blank"}
- [PRISM](http://www.prismmodelchecker.org/){:target="_blank"}
- [Uppaal SMC](http://people.cs.aau.dk/~adavid/smc/){:target="_blank"}

If you feel there is a tool that we forgot about, do not hesitate to [contact us]({{ site.baseurl }}/about.html#people).

[^1]: [Budde, C.E., D’Argenio, P.R., Monti, R.E., Compositional construction of import- ance functions in fully automated importance splitting, 2016](http://dsg.famaf.unc.edu.ar/node/691){:target="_blank"}

# Dependencies

A large number of dependencies contribute to the capabilities of Storm. We would like to thank the developers of these tools (in lexicographic order):

- [Boost](http://www.boost.org){:target="_blank"} (just-in-time compilation, parsing of the PRISM language & properties, various other tasks)
- [CArL](http://smtrat.github.io/carl/){:target="_blank"} (symbolic computations and as a wrapper for exact arithmetic, source of some cmake build scripts)
- [CLN](http://www.ginac.de/CLN/){:target="_blank"} (exact number representation)
- [CMake](https://cmake.org){:target="_blank"} (build system)
- [CUDD](http://vlsi.colorado.edu/~fabio/CUDD/cudd.pdf){:target="_blank"} (an MTBDD library available in the DD-related engines)
- [Eigen](http://eigen.tuxfamily.org/index.php?title=Main_Page){:target="_blank"} (sparse linear algebra)
- [gmm++](http://download.gna.org/getfem/html/homepage/gmm/){:target="_blank"} (sparse linear algebra)
- [GTest](https://github.com/google/googletest){:target="_blank"} (testing infrastructure)
- [L3pp](https://github.com/hbruintjes/l3pp){:target="_blank"} (logging)
- [PRISM](http://www.prismmodelchecker.org){:target="_blank"} (source of some CUDD extensions for MTBDD based model checking)
- [Sylvan](http://fmt.cs.utwente.nl/tools/sylvan/){:target="_blank"} (an MTBDD library available in the DD-related engines)
- [Xerces](https://xerces.apache.org){:target="_blank"} (XML parsing)
- [Z3](https://github.com/Z3Prover/z3/wiki){:target="_blank"} (SMT solving)

# Website

This website (including the website for [Stormpy](https://moves-rwth.github.io/stormpy/) and for our benchmarks) is developed with the following libraries, which made this effort so much easier:

- [Bootstrap](http://getbootstrap.com/){:target="_blank"}
- [BootstrapCDN](https://www.bootstrapcdn.com/){:target="_blank"}
- [Flatly Theme](https://bootswatch.com/flatly/){:target="_blank"}
- [Jekyll](https://jekyllrb.com){:target="_blank"}
- [DataTables](https://datatables.net){:target="_blank"}
- [Sphinx Doc](http://www.sphinx-doc.org/en/stable/){:target="_blank"}
- [Font awesome](http://fontawesome.io){:target="_blank"}
- [Font mfizz](http://fizzed.com/oss/font-mfizz){:target="_blank"}

# Development

We are happy to use the [Xcode](https://developer.apple.com/xcode/){:target="_blank"}, [CLion](https://www.jetbrains.com/clion/){:target="_blank"}, [Valgrind](http://valgrind.org/){:target="_blank"}, [Git](https://en.wikipedia.org/wiki/Git){:target="_blank"} and [Jenkins](https://jenkins.io/){:target="_blank"} for development purposes.