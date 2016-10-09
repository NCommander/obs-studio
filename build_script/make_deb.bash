fpm -n obsftl -v 16.2-ftl.11 --prefix /opt/obsftl -C /opt/obsftl -s dir -t deb -p ../../NAME_VERSION_ARCH.deb \
	--after-install deb_post_install \
	--after-remove deb_post_remove
