#include <curl/curl.h>

#include <cstdlib>
#include <iostream>

int main() {
	const auto *identity = curl_version_info(CURLVERSION_NOW);
	if (!identity || !identity->version || identity->version[0] == '\0') {
		std::cerr << "libcurl runtime identity is unavailable" << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << identity->version << std::endl;
	return EXIT_SUCCESS;
}
