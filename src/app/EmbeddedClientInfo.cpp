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
		<display>bornrok.com</display>
      		<address>play.bornrok.com</address>
      		<port>16900</port>
      		<version>20</version>
      		<langtype>14</langtype>
		<registrationweb>https://bornrok.com/</registrationweb>
		<loading>
			<image>loading00.jpg</image>
			<image>loading01.jpg</image>
			<image>loading02.jpg</image>
			<image>loading03.jpg</image>
			<image>loading04.jpg</image>
			<image>loading05.jpg</image>
			<image>loading06.jpg</image>
		</loading>
   	</connection>
</clientinfo>
)CI";
    return kXml;
}

}  // namespace uaro
