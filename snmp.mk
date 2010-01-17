# TODO: XXX: CURRENTLY SNMP NOT SUPPORTED!!!!
#
# Uncomment the following eight lines if you want to use David Thaler's
# CMU SNMP daemon support.
#
#SNMPDEF=	-DSNMP
#SNMPLIBDIR=	-Lsnmpd -Lsnmplib
#SNMPLIBS=	-lsnmpd -lsnmp
#CMULIBS=	snmpd/libsnmpd.a snmplib/libsnmp.a
#MSTAT=		mstat
#SNMP_SRCS=	snmp.c
#SNMP_OBJS=	snmp.o
#SNMPCLEAN=	snmpclean
# End SNMP support

#CONFIGCONFIGCONFIG
# Uncomment the following line if you want to use RSRR (Routing
# Support for Resource Reservations), currently used by RSVP.
#RSRRDEF=	-DRSRR

