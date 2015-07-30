FROM alpine:latest

RUN apk update
RUN apk add alpine-sdk linux-headers ncurses-dev readline-dev autoconf flex bison 

#COPY . /code
WORKDIR /code
#RUN autoconf
#RUN ./configure  --with-protocols="bgp pipe static" --enable-ipv6 --enable-pthreads
#RUN make
#RUN rm bird bird6 birdc birdcl
#RUN make CC="gcc - static"

