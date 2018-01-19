# Release Process

-  Edit `BIRD_VERSION` in `sysdep/config.h`
-  Create a new branch for your release (e.g. `v0.2.2-branch`)
-  Wait for CircleCI to build the branch.
-  Cut a new release (create a tag with the correct version name), copy across
   the artifacts from CircleCI.
-  On DockerHub:
  -  Add a build for the new version and trigger a build.
  -  Update the `latest` build to point to the new version tag and trigger a re-build.
-  Delete the temporary branch
