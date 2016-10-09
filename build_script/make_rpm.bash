fpm --rpm-summary "OBS-Studio FTL (faster than light)" \
	-n obsftl -v 16.2-ftl.11 --prefix /opt/obsftl -C /opt/obsftl -s dir -t rpm -p ../../NAME_VERSION_ARCH.rpm \
	--after-install rpm_post_install \
	--after-remove rpm_post_remove
	
