FROM alpine:latest
MAINTAINER Tom Denham <tom@projectcalico.org>

ADD . /code
RUN /code/build.sh
ADD start.sh /
CMD ["/start.sh"]
