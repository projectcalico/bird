docker build -f Dockerfile.build -t birdbuild .
mkdir -p dist
docker run --name bird-build -v `pwd`:/code birdbuild ./create_binaries.sh
docker rm -f bird-build || true
