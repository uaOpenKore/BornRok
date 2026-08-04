// Baked-in default sclientinfo.xml (S.: "зашей ещё этот xml в exe"), so a bare exe knows the server
// to connect to with no external data/sclientinfo.xml. An on-disk copy (in the VFS) still overrides
// it -- see Application::reloadClientInfo. Kept as a raw string literal (the XML has no )CI" run).

#include <string>

namespace uaro {

const std::string& embedded_clientinfo() {
    static const std::string kXml = R"CI(<?xml version="1.0" encoding="euc-kr" ?>
<clientinfo>
	<desc>BornRok</desc>
	<servicetype>korea</servicetype>
	<servertype>primary</servertype>
	<connection>
		<display>BornRok.com</display>
		<address>play.bornrok.com</address>
		<port>6900</port>
		<version>20</version>
		<langtype>14</langtype>
		<registrationweb>https://bornrok.com/</registrationweb>
	</connection>
	<connection>
		<display>UaRO.kiev.ua</display>
		<address>play.uaro.kiev.ua</address>
		<port>6900</port>
		<version>20</version>
		<langtype>14</langtype>
		<registrationweb>https://uaro.kiev.ua/</registrationweb>
	</connection>
</clientinfo>
)CI";
    return kXml;
}

}  // namespace uaro
