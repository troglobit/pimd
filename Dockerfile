FROM alpine:3.9

COPY . /root/pimd
WORKDIR /root/pimd
RUN apk add --update build-base automake autoconf linux-headers
RUN ./autogen.sh
RUN ./configure --prefix=/usr --sysconfdir=/etc
RUN make

FROM alpine:3.9
COPY --from=0 /root/pimd/src/pimd /root/pimd/src/pimctl /usr/sbin/

CMD [ "/usr/sbin/pimd", "--foreground" ]
