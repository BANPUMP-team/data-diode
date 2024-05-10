# data-diode software

Static arp should be set for each side of the data diode.

Place the following line in a crontab script for the root user if you want to upload files from a SRCDIR directory when unchanged for 1 minute:

	sendfiles=`find /path/to/SRCDIR -type f  -iname "*.*" -mmin +1 -exec lsattr {} + | grep  -v -- '----i-d---------------' | sed 's/----------------------//g'|sed 's/\.\//\"/g'|sed -z 's/\n/\"\n/g'`; if [ -n "$sendfiles" ]; then echo -n "$sendfiles" | xargs -n1 | xargs -I% chattr +i %; fi; echo -n "$sendfiles" | xargs -n1 | xargs -I% datadiode-send REMOTE_IP PORT % 4 6; if [ -n "$sendfiles" ]; then echo -n "$sendfiles" | xargs -n1 | xargs -d '\n' -I% chattr +d %; fi 

A receiver must be listening on the correct IP and PORT on the other side:

	datadiode-recv PORT /path/to/DSTDIR 

For automatic recovery of incoming files use inotify-tools:

	inotifywait -F -m /path/to/DSTDIR -e create --include '.*\.finished$' | while read -r directory action file; do datadiode-recovery /path/to/DSTDIR "${file%.finished}" 4; done; 


Another example is for MySQL/MariaDB incremental backup. The tools are mysqlbackup/mariabackup/xtrabackup:

On the sender side:
	tar -cvzf - xtrabackup/* | split -b 1024M - "DD-part" 
	lastone=`ls DD-part* | tail -n1`  
	mv "$lastone" "$lastone".EOF 

On the receiver side:
	lastone=`ls DD-part*.EOF`  
	mv "$lastone" "${lastone%.EOF}" 
	cat DD-part* > xtrabackup.tgz 

If you'd like to use vsftpd instead of sshfs or other tools for uploading files into /path/to/SRCDIR vsftpd is the right choice.

For Syslog via data diode you need to enable UDP server in /etc/rsyslog.conf or inside the included config file /etc/rsyslog.d/remote.conf:

module(load="imudp")
input(type="imudp" port="514" Address="127.0.0.1")

on the destination server. This is the syslog input for the concentrator where datadiode-syslog-deamplifier will send data to. 

Client syslog daemons should send data into the datadiode-syslog-amplifier:

*.* @datadiode-amplifier-host-ip:1514

 Similar settings for BSD syslog and syslog-ng if they are the choice over rsyslog.
