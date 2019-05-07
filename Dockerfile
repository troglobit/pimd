FROM alpine:3.9
RUN apk add --update git build-base automake autoconf linux-headers

RUN git clone --depth=1 https://github.com/troglobit/pimd.git /root/pimd
WORKDIR /root/pimd

RUN ./autogen.sh
RUN ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var
RUN make
RUN make install-strip DESTDIR=/tmp

FROM alpine:3.9
COPY --from=0 /tmp/usr/sbin/pimd /tmp/usr/sbin/pimctl /usr/sbin/

CMD [ "/usr/sbin/pimd", "--foreground" ]
