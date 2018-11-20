# Release Process

-  Edit `BIRD_VERSION` in `sysdep/config.h`
-  Create a tag with the new version number (e.g. `v0.3.3`)
-  Push the `sysdep/config.h` and the tag to https://github.com/projectcalico/bird

[Semaphore](https://semaphoreci.com/calico/bird/branches/feature-ipinip)
will then build the new code and push images to DockerHub that are
tagged with the new version number.
