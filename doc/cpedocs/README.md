# CPEDocs

This directory contains material for automating the deployment of CTI documentation in
the CPEDocs project.

https://github.hpe.com/hpe/CPEDocs

## Maintenance

In general, just ensure that `./collect_cpedocs.sh` doesn't break and its generated
content doesn't become outdated.

### Maintaining `index.md`

`index.md` is included in-place in the CPEDocs debugging tools top level index file.

Since it is part of a larger file, the highest level header level should be 2
(##). Assume that CTI content will live in a directory called `./cti` for
links. The `cti` directory is created by the `collect_cpedocs.sh` script.
