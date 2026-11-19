To build RPM package need to execute
```sh
mkdir -p ~/rpmbuild/SOURCES
rpmbuild --undefine=_disable_source_fetch -ba libnats.spec
```

If you need specific package version, please edit "Version" value inside spec file.

