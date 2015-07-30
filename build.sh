docker build -t birdbuild .
docker run --rm -v `pwd`:/code birdbuild sh -c '\
autoconf && \
./configure  --with-protocols="bgp pipe static" --enable-ipv6=yes --enable-client=no --enable-pthreads=yes && \
make && \
rm bird && \
make CC="gcc -static"
'
mkdir -p dist
cp bird dist

