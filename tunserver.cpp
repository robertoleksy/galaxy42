/**ZZZZZZZZZ
Copyrighted (C) 2016, GPL v3 Licence (may include also other code)
See LICENCE.txt
*/

/*

TODO(r) do not tunnel entire (encrypted) copy of TUN, trimm it from headers that we do not need
TODO(r) establish end-to-end AE (cryptosession)

*/

/*

Use this tags in this project:
[confroute] - configuration, tweak - for the routing
[protocol] - code related to how exactly protocol (e.g. node2node) is defined

*/

const char * disclaimer = "*** WARNING: This is a work in progress, do NOT use this code, it has bugs, vulns, and 'typpos' everywhere! ***"; // XXX

#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/program_options.hpp>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <string.h>
#include <assert.h>

#include <thread>

#include <cstring>

#include <sodium.h>

#include "libs1.hpp"
#include "counter.hpp"
#include "cpputils.hpp"

// linux (and others?) select use:
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>

// for low-level Linux-like systems TUN operations
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include<netinet/ip_icmp.h>   //Provides declarations for icmp header
#include<netinet/udp.h>   //Provides declarations for udp header
#include<netinet/tcp.h>   //Provides declarations for tcp header
#include<netinet/ip.h>    //Provides declarations for ip header
// #include <net/if_ether.h> // peer over eth later?
// #include <net/if_media.h> // ?

#include "cjdns-code/NetPlatform.h" // from cjdns

//#include "crypto-sodium/ecdh_ChaCha20_Poly1305.hpp"
// #include <net/if_tap.h>
#include <linux/if_tun.h>

#include "c_ip46_addr.hpp"
#include "c_peering.hpp"

#include "crypto.hpp" // for tests

#include "crypto-sodium/ecdh_ChaCha20_Poly1305.hpp"

// #include "trivialserialize.hpp"

// ------------------------------------------------------------------

void error(const std::string & msg) {
	std::cout << "Error: " << msg << std::endl;
	throw std::runtime_error(msg);
}

// ------------------------------------------------------------------



namespace developer_tests {

bool wip_strings_encoding(boost::program_options::variables_map & argm) {
	_mark("Tests of string encoding");
	string s1,s2,s3;
	using namespace std;
	s1="4a4b4c4d4e"; // in hex
//	s2="ab"; // in b64
	s3="y"; // in bin


	// TODO assert is results are as expected!
	// TODO also assert that the exceptions are thrown as they should be, below

	auto s1_hex = string_as_hex( s1 );
	c_haship_pubkey pub1( s1_hex );
	_info("pub = " << to_string(pub1));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex("4"))));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex("f4b4c4d4e"))));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex("4a4b4c4d4e"))));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex(""))));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex("ffffffff"))));
	_info("pub = " << to_string(c_haship_pubkey(string_as_hex("00000000"))));
	try {
		_info("pub = " << to_string(c_haship_pubkey(string_as_hex("4a4b4c4d4eaba46381826328363782917263521719badbabdbadfade7455467383947543473839474637293474637239273534873"))));
	} catch (std::exception &e) { _note("Test failed, as expected: " << e.what()); }
	try {
		_info("pub = " << to_string(c_haship_pubkey(string_as_hex("0aq"))));
	} catch (std::exception &e) { _note("Test failed, as expected: " << e.what()); }
	try {
		_info("pub = " << to_string(c_haship_pubkey(string_as_hex("qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"))));
	} catch (std::exception &e) { _note("Test failed, as expected: " << e.what()); }

//	c_haship_pubkey pub2( string_as_b64( s1 ) );
//	c_haship_pubkey pub3( string_as_bin( s1 ) );

	_info("Test done");
	return false;
}

} // namespace

// ------------------------------------------------------------------

/***
  @brief interface for object that can act as p2p node
*/
class c_galaxy_node {
	public:
		c_galaxy_node()=default;
		virtual ~c_galaxy_node()=default;

		virtual void nodep2p_foreach_cmd( c_protocol::t_proto_cmd cmd, string_as_bin data )=0; ///< send given command/data to each peer
		virtual const c_peering & get_peer_with_hip( c_haship_addr addr )=0; ///< return peering reference of a peer by given HIP. Will throw expected_not_found
};

// ------------------------------------------------------------------

class c_routing_manager { ///< holds nowledge about routes, and searches for new ones
	public: // make it private, when possible - e.g. when all operator<< are changed to public: print(ostream&) const;
		enum t_route_state { e_route_state_found, e_route_state_dead };

		enum t_search_mode {  // why we look for a route
			e_search_mode_route_own_packet, // we want ourselves to send there
			e_search_mode_route_other_packet,  // we want to route packet of someone else
			e_search_mode_help_find }; // some else is asking us about the route

		typedef	std::chrono::steady_clock::time_point t_route_time; ///< type for representing times using in routing search etc

		class c_route_info {
			public:
				t_route_state m_state; ///< e.g. e_route_state_found is route is ready to be used
				c_haship_addr m_nexthop; ///< hash-ip of next hop in this route

				int m_cost; ///< some general cost - currently e.g. in number of hops
				t_route_time m_time; ///< age of this route
				// int m_ttl; ///< at which TTL we got this reply

				c_route_info(c_haship_addr nexthop, int cost);

				int get_cost() const;
		};

		class c_route_reason {
			public:
				c_haship_addr m_his_addr; ///< his address to which we should tell him the path
				t_search_mode m_search_mode; ///< do we search it for him because we need to route for him, or because he asked, etc
				// c_haship_addr m_his_question; ///< the address about which we aksed

				c_route_reason(c_haship_addr his_addr, t_search_mode mode);

				bool operator<(const c_route_reason &other) const;
				bool operator==(const c_route_reason &other) const;
		};

		class c_route_reason_detail {
			public:
				t_route_time m_when; ///< when we hasked about this address last time
				int m_ttl; ///< with what TTL we are doing the search
				c_route_reason_detail( t_route_time when , int ttl );
		};

		class c_route_search {
			public:
				c_haship_addr m_addr; ///< goal of search: dst address
				bool m_ever; ///< was this ever actually searched yet
				t_route_time m_ask_time; ///< at which time we last time tried asking

				int m_ttl_used; ///< at which TTL we actually last time tried asking
				int m_ttl_should_use; ///< at which TTL we want to search, looking at our requests (this is optimization - it's same as highest value in m_requests[])
				// TODO(r)
				// guy ttl=4 --> ttl3 --> ttl2 --> ttl1 *MYSELF*, highest_ttl=1, when we execute then: send ttl=0, set ask_ttl=0
				// ... meanwhile ...
				//                    guy ttl4 --> ttl3 *MYSELF*, highest_ttl=3(!!!), when we execute then: send ttl=2 (when timeout!) then ask_ttl=2

				map< c_route_reason , c_route_reason_detail > m_request; ///< information about all other people who are asking about this address

				c_route_search(c_haship_addr addr, int basic_ttl);

				void add_request(c_routing_manager::c_route_reason reason, int ttl); ///< add info that this guy also wants to be informed about the path
				void execute( c_galaxy_node & galaxy_node );
		};

		// searches:
		typedef std::map< c_haship_addr, unique_ptr<c_route_search> > t_route_search_by_dst; ///< running searches, by the hash-ip of finall destination
		t_route_search_by_dst m_search; ///< running searches

		// known routes:
		typedef std::map< c_haship_addr, unique_ptr<c_route_info> > t_route_nexthop_by_dst; ///< routes to destinations: the hash-ip of next hop, by hash-ip of finall destination
		t_route_nexthop_by_dst m_route_nexthop; ///< known routes: the hash-ip of next hop, indexed by hash-ip of finall destination

		const c_route_info & add_route_info_and_return(c_haship_addr target, c_route_info route_info); ///< learn a route to this target. If it exists, then merge it correctly (e.g. pick better one)

	public:
		const c_route_info & get_route_or_maybe_search(c_galaxy_node & galaxy_node , c_haship_addr dst, c_routing_manager::c_route_reason reason, bool start_search, int search_ttl);
};

std::ostream & operator<<(std::ostream & ostr, std::chrono::steady_clock::time_point tp) {
	using namespace std::chrono;
	steady_clock::duration dtn = tp.time_since_epoch();
	return ostr << duration_cast<seconds>(dtn).count();
}

std::ostream & operator<<(std::ostream & ostr, const c_routing_manager::t_search_mode & obj) {
	switch (obj) {
		case c_routing_manager::e_search_mode_route_own_packet: return ostr<<"route_OWN";
		case c_routing_manager::e_search_mode_route_other_packet: return ostr<<"route_OTHER";
		case c_routing_manager::e_search_mode_help_find: return ostr<<"help_FIND";
	}
	_warn("Unknown reason"); return ostr<<"???";
}

std::ostream & operator<<(std::ostream & ostr, const c_routing_manager::c_route_info & obj) {
	return ostr << "{ROUTE: next_hop=" << obj.m_nexthop
		<< " cost=" << obj.m_cost << " time=" << obj.m_time << "}";
}

std::ostream & operator<<(std::ostream & ostr, const c_routing_manager::c_route_reason & obj) {
	return ostr << "{Reason: asked from " << obj.m_his_addr << " as " << obj.m_search_mode << "}";
}
std::ostream & operator<<(std::ostream & ostr, const c_routing_manager::c_route_reason_detail & obj) {
	return ostr << "{Reason...: at " << obj.m_when << " with TTL=" << obj.m_ttl << "}";
}

std::ostream & operator<<(std::ostream & ostr, const c_routing_manager::c_route_search & obj) {
	ostr << "{SEARCH for route to DST="<<obj.m_addr<<", was yet run=" << (obj.m_ever?"YES":"never")
		<< " ask: time="<<obj.m_ask_time<<" ttl should="<<obj.m_ttl_should_use << ", ttl used=" << obj.m_ttl_used;
	if (obj.m_request.size()) {
		ostr << "with " << obj.m_request.size() << " REQUESTS:" << endl;
		for(auto const & r : obj.m_request) ostr << " REQ: " << r.first << " => " << r.second << endl;
		ostr << endl;
	} else ostr << " (no requesters here)";
	ostr << "}";
	return ostr;
}


c_routing_manager::c_route_info::c_route_info(c_haship_addr nexthop, int cost)
	: m_nexthop(nexthop), m_cost(cost), m_time(  std::chrono::steady_clock::now() )
{ }

int c_routing_manager::c_route_info::get_cost() const { return m_cost; }

c_routing_manager::c_route_reason_detail::c_route_reason_detail( t_route_time when , int ttl )
	: m_when(when) , m_ttl ( ttl )
{ }

void c_routing_manager::c_route_search::add_request(c_routing_manager::c_route_reason reason , int ttl) {
	auto found = m_request.find( reason );
	if (found == m_request.end()) { // new reason for search
		c_route_reason_detail reason_detail( std::chrono::steady_clock::now() , ttl );
		_info("Adding new reason for search: " << reason << " details: " << reason_detail);
		m_request.emplace(reason, reason_detail);
	}
	else {
		auto & detail = found->second;
		_info("Updating reason of search: " << reason << " old detail: " << detail );
		detail.m_when = std::chrono::steady_clock::now();
		detail.m_ttl = std::max( detail.m_ttl , ttl ); // use the bigger TTL [confroute]
		_info("Updating reason of search: " << reason << " new detail: " << detail );
	}

	// update this search'es goal TTL
	// TODO(r)-refact: this could be factored into some generic: set_highest() , with optional debug too
	auto ttl_old = this->m_ttl_should_use;
	this->m_ttl_should_use = std::max( this->m_ttl_should_use , ttl);
	if (ttl_old != this->m_ttl_should_use) _info("Updated this search TTL to " << this->m_ttl_should_use << " from " << ttl_old);
}

c_routing_manager::c_route_reason::c_route_reason(c_haship_addr his_addr, t_search_mode mode)
	: m_his_addr(his_addr), m_search_mode(mode)
{
	_info("NEW reason: "<< (*this));
}

bool c_routing_manager::c_route_reason::operator<(const c_route_reason &other) const {
	if (this->m_his_addr < other.m_his_addr) return 1;
	if (this->m_search_mode < other.m_search_mode) return 1;
	return 0;
}

bool c_routing_manager::c_route_reason::operator==(const c_route_reason &other) const {
	return (this->m_his_addr == other.m_his_addr) && (this->m_search_mode == other.m_search_mode);
}

c_routing_manager::c_route_search::c_route_search(c_haship_addr addr, int basic_ttl)
	: m_addr(addr), m_ever(false), m_ask_time(), m_ttl_used(0), m_ttl_should_use(5)
{
	_info("NEW router SEARCH: " << (*this));
}

const c_routing_manager::c_route_info & c_routing_manager::add_route_info_and_return(c_haship_addr target, c_route_info route_info) {
	// TODO(r): refactor out the create-or-update idiom
	auto it = m_route_nexthop.find( target );
	if (it == m_route_nexthop.end()) { // new one
		_info("This is NEW route information." << route_info);
		auto new_obj = make_unique<c_route_info>( route_info ); // TODO(rob): std::move it here - optimization?
		auto emplace = m_route_nexthop.emplace( std::move(target) , std::move(new_obj) );
		assert(emplace.second == true); // inserted new
		return * emplace.first->second; // reference to object stored in member we own
	} else {
		_info("This is UPDATED route information." << route_info);
		// TODO(r) TODONEXT pick optimal path?
		return * it->second;
	}
}

const c_routing_manager::c_route_info & c_routing_manager::get_route_or_maybe_search(c_galaxy_node & galaxy_node, c_haship_addr dst, c_routing_manager::c_route_reason reason, bool start_search , int search_ttl) {
	_info("ROUTING-MANAGER: find: " << dst << ", for reason: " << reason );

	try {
		const auto & peer = galaxy_node.get_peer_with_hip(dst);
		_info("We have that peer directly: " << peer );
		const int cost = 1; // direct peer. In future we can add connection cost or take into account congestion/lag...
		c_route_info route_info( peer.get_hip() , cost  );
		_info("Direct route: " << route_info);
		const auto & route_info_ref_we_own = this -> add_route_info_and_return( dst , route_info ); // store it, so that we own this object
		return route_info_ref_we_own; // <--- return direct
	} 
	catch(expected_not_found) { } // not found in direct peers

	auto found = m_route_nexthop.find( dst ); // <--- search what we know
	if (found != m_route_nexthop.end()) { // found
		const auto & route = found->second;
		_info("ROUTING-MANAGER: found route: " << (*route));
		return *route; // <--- warning: refrerence to this-owned object that is easily invalidatd
	}
	else { // don't have a planned route to him
		if (!start_search) {
			_info("No route, but we also so not want to search for it.");
			throw std::runtime_error("no route known (and we do NOT WANT TO search) to dst=" + STR(dst));
		}
		else {
			_info("Route not found, we will be searching");
			bool created_now=false;
			auto search_iter = m_search.find(dst);
			if (search_iter == m_search.end()) {
				created_now=true;
				_info("STARTED SEARCH (created brand new search record) for route to dst="<<dst);
				auto new_search = make_unique<c_route_search>(dst, search_ttl); // start a new search, at this TTL
				new_search->add_request( reason , search_ttl ); // add a first reason (it also sets TTL)
				auto search_emplace = m_search.emplace( std::move(dst) , std::move(new_search) );

				assert(search_emplace.second == true); // the insertion took place
				search_iter = search_emplace.first; // save here the result
			}
			else {
				_info("STARTED SEARCH (updated an existing search) for this to dst="<<dst);
				search_iter->second->add_request( reason , search_ttl ); // add reason (can increase TTL)
			}
			auto & search_obj = search_iter->second; // search exists now (new or updated)
			if (created_now) search_obj->execute( galaxy_node ); // ***
		}
	}
	_note("NO ROUTE");
	throw std::runtime_error("NO ROUTE known (at current time) to dst=" + STR(dst));
}

void  c_routing_manager::c_route_search::execute( c_galaxy_node & galaxy_node ) {
	_info("Sending QUERY for HIP, with m_ttl_should_use=" << m_ttl_should_use);
	string_as_bin data; // [protocol] for search query - format is: HIP_BINARY;TTL_BINARY;

	data += string_as_bin(m_addr);
	data += string(";");

	unsigned char byte_highest_ttl = m_ttl_should_use;  assert( m_ttl_should_use == byte_highest_ttl ); // TODO(r) asserted narrowing
	data += string(1, static_cast<char>(byte_highest_ttl) );
	data += string(";");

	galaxy_node.nodep2p_foreach_cmd( c_protocol::e_proto_cmd_findhip_query , data );

	m_ttl_used = byte_highest_ttl;
	m_ask_time = std::chrono::steady_clock::now();
}



// ------------------------------------------------------------------

class c_tunserver : public c_galaxy_node {
	public:
		c_tunserver();
		void configure_mykey_from_string(const std::string &mypub, const std::string &mypriv);
		void run(); ///< run the main loop
		void add_peer(const t_peering_reference & peer_ref); ///< add this as peer
		void add_peer_simplestring(const string & simple); ///< add this as peer, from a simple string like "ip-pub" TODO(r) instead move that to ctor of t_peering_reference
		void set_my_name(const string & name); ///< set a nice name of this peer (shown in debug for example)

		void help_usage() const; ///< show help about usage of the program

		typedef enum {
			e_route_method_from_me=1, ///< I am the oryginal sender (try hard to send it)
			e_route_method_if_direct_peer=2, ///< Send data only if if I know the direct peer (e.g. I just route it for someone else - in star protocol the center node)
			e_route_method_default=3, ///< The default routing method
		} t_route_method;

		void nodep2p_foreach_cmd(c_protocol::t_proto_cmd cmd, string_as_bin data) override;
		const c_peering & get_peer_with_hip( c_haship_addr addr ) override;

	protected:
		void prepare_socket(); ///< make sure that the lower level members of handling the socket are ready to run
		void event_loop(); ///< the main loop
		void wait_for_fd_event(); ///< waits for event of I/O being ready, needs valid m_tun_fd and others, saves the fd_set into m_fd_set_data

		std::pair<c_haship_addr,c_haship_addr> parse_tun_ip_src_dst(const char *buff, size_t buff_size, unsigned char ipv6_offset); ///< from buffer of TUN-format, with ipv6 bytes at ipv6_offset, extract ipv6 (hip) destination
		std::pair<c_haship_addr,c_haship_addr> parse_tun_ip_src_dst(const char *buff, size_t buff_size); ///< the same, but with ipv6_offset that matches our current TUN

		///@brief push the tunneled data to where they belong. On failure returns false or throws, true if ok.
		bool route_tun_data_to_its_destination_top(t_route_method method, const char *buff, size_t buff_size, c_routing_manager::c_route_reason reason, int data_route_ttl);

		///@brief more advanced version for use in routing
		bool route_tun_data_to_its_destination_detail(t_route_method method, const char *buff, size_t buff_size, c_routing_manager::c_route_reason reason, c_haship_addr next_hip,
			int recurse_level, int data_route_ttl);

		void peering_ping_all_peers();
		void debug_peers();

	private:
		string m_my_name; ///< a nice name, see set_my_name
		int m_tun_fd; ///< fd of TUN file
		unsigned char m_tun_header_offset_ipv6; ///< current offset in TUN/TAP data to the position of ipv6

		int m_sock_udp; ///< the main network socket (UDP listen, send UDP to each peer)

		fd_set m_fd_set_data; ///< select events e.g. wait for UDP peering or TUN input

		typedef std::map< c_haship_addr, unique_ptr<c_peering> > t_peers_by_haship; ///< peers (we always know their IPv6 - we assume here), indexed by their hash-ip
		t_peers_by_haship m_peer; ///< my peers, indexed by their hash-ip

		c_haship_pubkey m_haship_pubkey; ///< pubkey of my IP
		c_haship_addr m_haship_addr; ///< my haship addres
		c_peering & find_peer_by_sender_peering_addr( c_ip46_addr ip ) const ;

		c_routing_manager m_routing_manager; ///< the routing engine used for most things
};

// ------------------------------------------------------------------

using namespace std; // XXX move to implementations, not to header-files later, if splitting cpp/hpp

void c_tunserver::add_peer_simplestring(const string & simple) {
	size_t pos1 = simple.find('-');
	string part_ip = simple.substr(0,pos1);
	string part_pub = simple.substr(pos1+1);
	_note("Simple string parsed as: " << part_ip << " and " << part_pub );
	this->add_peer( t_peering_reference( part_ip, string_as_hex( part_pub ) ) );
}

c_tunserver::c_tunserver()
 : m_my_name("unnamed-tunserver"), m_tun_fd(-1), m_tun_header_offset_ipv6(0), m_sock_udp(-1)
{
}

void c_tunserver::set_my_name(const string & name) {  m_my_name = name; _note("This node is now named: " << m_my_name);  }

// my key
void c_tunserver::configure_mykey_from_string(const std::string &mypub, const std::string &mypriv) {
	m_haship_pubkey = string_as_bin( string_as_hex( mypub ) );
	m_haship_addr = c_haship_addr( c_haship_addr::tag_constr_by_hash_of_pubkey() , m_haship_pubkey );
	_info("Configuring the router, I am: pubkey="<<to_string(m_haship_pubkey)<<" ip="<<to_string(m_haship_addr)
		<<" privkey="<<mypriv);
}

// add peer
void c_tunserver::add_peer(const t_peering_reference & peer_ref) { ///< add this as peer
	_note("Adding peer from reference=" << peer_ref
		<< " that reads: " << "peering-address=" << peer_ref.peering_addr << " pubkey=" << to_string(peer_ref.pubkey) << " haship_addr=" << to_string(peer_ref.haship_addr) );
	auto peering_ptr = make_unique<c_peering_udp>(peer_ref);
	// TODO(r) check if duplicated peer (map key) - warn or ignore dep on parameter
	m_peer.emplace( std::make_pair( peer_ref.haship_addr ,  std::move(peering_ptr) ) );
}

void c_tunserver::help_usage() const {
	// TODO(r) remove, using boost options
}

void c_tunserver::prepare_socket() {
	m_tun_fd = open("/dev/net/tun", O_RDWR);
	assert(! (m_tun_fd<0) );

  as_zerofill< ifreq > ifr; // the if request
	ifr.ifr_flags = IFF_TUN; // || IFF_MULTI_QUEUE; TODO
	strncpy(ifr.ifr_name, "galaxy%d", IFNAMSIZ);

	auto errcode_ioctl =  ioctl(m_tun_fd, TUNSETIFF, (void *)&ifr);
	m_tun_header_offset_ipv6 = g_tuntap::TUN_with_PI::header_position_of_ipv6; // matching the TUN/TAP type above
	if (errcode_ioctl < 0)_throw( std::runtime_error("Error in ioctl")); // TODO

	_mark("Allocated interface:" << ifr.ifr_name);

	{
		uint8_t address[16];
		for (int i=0; i<16; ++i) address[i] = m_haship_addr.at(i);
		// TODO: check if there is no race condition / correct ownership of the tun, that the m_tun_fd opened above is...
		// ...to the device to which we are setting IP address here:
		assert(address[0] == 0xFD);
		assert(address[1] == 0x42);
		NetPlatform_addAddress(ifr.ifr_name, address, 16, Sockaddr_AF_INET6);
	}

	// create listening socket
	m_sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
	_assert(m_sock_udp >= 0);

	int port = 9042;
	c_ip46_addr address_for_sock = c_ip46_addr::any_on_port(port);

	{
		int bind_result = -1;
		if (address_for_sock.get_ip_type() == c_ip46_addr::t_tag::tag_ipv4) {
			sockaddr_in addr4 = address_for_sock.get_ip4();
			bind_result = bind(m_sock_udp, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));  // reinterpret allowed by Linux specs
		}
		else if(address_for_sock.get_ip_type() == c_ip46_addr::t_tag::tag_ipv6) {
			sockaddr_in6 addr6 = address_for_sock.get_ip6();
			bind_result = bind(m_sock_udp, reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));  // reinterpret allowed by Linux specs
		}
			_assert( bind_result >= 0 ); // TODO change to except
			_assert(address_for_sock.get_ip_type() != c_ip46_addr::t_tag::tag_none);
	}
	_info("Bind done - listening on UDP on: "); // TODO  << address_for_sock
}

void c_tunserver::wait_for_fd_event() { // wait for fd event
	_info("Selecting");
	// set the wait for read events:
	FD_ZERO(& m_fd_set_data);
	FD_SET(m_sock_udp, &m_fd_set_data);
	FD_SET(m_tun_fd, &m_fd_set_data);

	auto fd_max = std::max(m_tun_fd, m_sock_udp);
	_assert(fd_max < std::numeric_limits<decltype(fd_max)>::max() -1); // to be more safe, <= would be enough too
	_assert(fd_max >= 1);

	timeval timeout { 3 , 0 }; // http://pubs.opengroup.org/onlinepubs/007908775/xsh/systime.h.html

	auto select_result = select( fd_max+1, &m_fd_set_data, NULL, NULL, & timeout); // <--- blocks
	_assert(select_result >= 0);
}

std::pair<c_haship_addr,c_haship_addr> c_tunserver::parse_tun_ip_src_dst(const char *buff, size_t buff_size) { ///< the same, but with ipv6_offset that matches our current TUN
	return parse_tun_ip_src_dst(buff,buff_size, m_tun_header_offset_ipv6 );
}

std::pair<c_haship_addr,c_haship_addr> c_tunserver::parse_tun_ip_src_dst(const char *buff, size_t buff_size, unsigned char ipv6_offset) {
	// vuln-TODO(u) throw on invalid size + assert

	size_t pos_src = ipv6_offset + g_ipv6_rfc::header_position_of_src , len_src = g_ipv6_rfc::header_length_of_src;
	size_t pos_dst = ipv6_offset + g_ipv6_rfc::header_position_of_dst , len_dst = g_ipv6_rfc::header_length_of_dst;
	assert(buff_size > pos_src+len_src);
	assert(buff_size > pos_dst+len_dst);
	// valid: reading pos_src up to +len_src, and same for dst

	char ipv6_str[INET6_ADDRSTRLEN]; // for string e.g. "fd42:ffaa:..."

	memset(ipv6_str, 0, INET6_ADDRSTRLEN);
	inet_ntop(AF_INET6, buff + pos_src, ipv6_str, INET6_ADDRSTRLEN); // ipv6 octets from 8 is source addr, from ipv6 RFC
	_dbg1("src ipv6_str " << ipv6_str);
	c_haship_addr ret_src(c_haship_addr::tag_constr_by_addr_string(), ipv6_str);

	memset(ipv6_str, 0, INET6_ADDRSTRLEN);
	inet_ntop(AF_INET6, buff + pos_dst, ipv6_str, INET6_ADDRSTRLEN); // ipv6 octets from 24 is destination addr, from
	_dbg1("dst ipv6_str " << ipv6_str);
	c_haship_addr ret_dst(c_haship_addr::tag_constr_by_addr_string(), ipv6_str);

	return std::make_pair( ret_src , ret_dst );
}

void c_tunserver::peering_ping_all_peers() {
	_info("Sending ping to all peers");
	for(auto & v : m_peer) { // to each peer
		auto & target_peer = v.second;
		auto peer_udp = unique_cast_ptr<c_peering_udp>( target_peer ); // upcast to UDP peer derived

		// [protocol] build raw
		string_as_bin cmd_data;
		cmd_data.bytes += string_as_bin( m_haship_pubkey ).bytes;
		cmd_data.bytes += ";";
		peer_udp->send_data_udp_cmd(c_protocol::e_proto_cmd_public_hi, cmd_data, m_sock_udp);
	}
}

void c_tunserver::nodep2p_foreach_cmd(c_protocol::t_proto_cmd cmd, string_as_bin data) {
	_info("Sending a COMMAND to peers:");
	size_t count_used=0; ///< peers that we used
	for(auto & v : m_peer) { // to each peer
		auto & target_peer = v.second;
		auto peer_udp = unique_cast_ptr<c_peering_udp>( target_peer ); // upcast to UDP peer derived
		peer_udp->send_data_udp_cmd(cmd, data, m_sock_udp);
	}
}

const c_peering & c_tunserver::get_peer_with_hip( c_haship_addr addr ) {
	auto peer_iter = m_peer.find(addr);
	if (peer_iter == m_peer.end()) throw expected_not_found();
	return * peer_iter->second;
}

void c_tunserver::debug_peers() {
	_note("=== Debug peers ===");
	for(auto & v : m_peer) { // to each peer
		auto & target_peer = v.second;
		_info("  * Known peer on key [ " << v.first << " ] => " << (* target_peer) );
	}
}

bool c_tunserver::route_tun_data_to_its_destination_detail(t_route_method method, const char *buff, size_t buff_size, c_routing_manager::c_route_reason reason,
	c_haship_addr next_hip, int recurse_level, int data_route_ttl)
{
	// --- choose next hop in peering ---

	// try direct peers:
	auto peer_it = m_peer.find(next_hip); // find c_peering to send to // TODO(r) this functionallity will be soon doubled with the route search in m_routing_manager below, remove it then

	if (peer_it == m_peer.end()) { // not a direct peer!
		_info("ROUTE: can not find in direct peers next_hip="<<next_hip);
		if (recurse_level>1) {
			_warn("DROP: Recruse level too big in choosing peer");
			return false; // <---
		}

		c_haship_addr via_hip;
		try {
			_info("Trying to find a route to it");
			const int default_ttl = c_protocol::ttl_max_accepted; // for this case [confroute]
			const auto & route = m_routing_manager.get_route_or_maybe_search(*this, next_hip , reason , true, default_ttl);
			_info("Found route: " << route);
			via_hip = route.m_nexthop;
		} catch(...) { _info("ROUTE MANAGER: can not find route at all"); return false; }
		_info("Route found via hip: via_hip = " << via_hip);
		bool ok = this->route_tun_data_to_its_destination_detail(method, buff, buff_size, reason,  via_hip, recurse_level+1, data_route_ttl);
		if (!ok) { _info("Routing failed"); return false; } // <---
		_info("Routing seems to succeed");
	}
	else { // next_hip is a direct peer, send to it:
		auto & target_peer = peer_it->second;
		_info("ROUTE-PEER (found the goal in direct peer) selected peerig next hop is: " << (*target_peer) );
		auto peer_udp = unique_cast_ptr<c_peering_udp>( target_peer ); // upcast to UDP peer derived
		peer_udp->send_data_udp(buff, buff_size, m_sock_udp, data_route_ttl); // <--- ***
	}
	return true;
}

bool c_tunserver::route_tun_data_to_its_destination_top(t_route_method method, const char *buff, size_t buff_size, c_routing_manager::c_route_reason reason, int data_route_ttl) {
	try {
		c_haship_addr dst_hip = parse_tun_ip_src_dst(buff, buff_size).second;
		_info("Destination HIP:" << dst_hip);
		bool ok = this->route_tun_data_to_its_destination_detail(method, buff, buff_size, reason,  dst_hip, 0, data_route_ttl);
		if (!ok) { _info("Routing/sending failed (top level)"); return false; }
	} catch(std::exception &e) {
		_warn("Can not send to peer, because:" << e.what()); // TODO more info (which peer, addr, number)
	} catch(...) {
		_warn("Can not send to peer (unknown)"); // TODO more info (which peer, addr, number)
	}
	_info("Routing/sending OK (top level)");
	return true;
}

c_peering & c_tunserver::find_peer_by_sender_peering_addr( c_ip46_addr ip ) const {
	for(auto & v : m_peer) { if (v.second->get_pip() == ip) return * v.second.get(); }
	throw std::runtime_error("We do not know a peer with such IP=" + STR(ip));
}


void c_tunserver::event_loop() {
	_info("Entering the event loop");
	c_counter counter(2,true);
	c_counter counter_big(10,false);

	fd_set fd_set_data;


	this->peering_ping_all_peers();
	const auto ping_all_frequency = std::chrono::seconds( 600 ); // how often to ping them
	const auto ping_all_frequency_low = std::chrono::seconds( 1 ); // how often to ping first few times
	const long int ping_all_count_low = 2; // how many times send ping fast at first

	auto ping_all_time_last = std::chrono::steady_clock::now(); // last time we sent ping to all
	long int ping_all_count = 0; // how many times did we do that in fact


	// low level receive buffer
	const int buf_size=65536;
	char buf[buf_size];

	bool anything_happened=false; // in given loop iteration, for e.g. debug

	while (1) {
		// std::this_thread::sleep_for( std::chrono::milliseconds(100) ); // was needeed to avoid any self-DoS in case of TTL bugs

		auto time_now = std::chrono::steady_clock::now(); // time now

		{
			auto freq = ping_all_frequency;
			if (ping_all_count < ping_all_count_low) freq = ping_all_frequency_low;
			if (time_now > ping_all_time_last + freq ) {
				_note("It's time to ping all peers again (at auto-pinging time frequency=" << std::chrono::duration_cast<std::chrono::seconds>(freq).count() << " seconds)");
				peering_ping_all_peers(); // TODO(r) later ping only peers that need that
				ping_all_time_last = std::chrono::steady_clock::now();
				++ping_all_count;
			}
		}

		ostringstream oss;
		oss <<	" Node " << m_my_name << " hip=" << m_haship_addr << " pubkey=" << m_haship_pubkey;
		const string node_title_bar = oss.str();


		if (anything_happened) {
			debug_peers();

			string xx(10,'-');
			std::cerr << endl << xx << node_title_bar << xx << endl << endl;
		} // --- print your name ---

		anything_happened=false;

		wait_for_fd_event();

		// TODO(r): program can be hanged/DoS with bad routing, no TTL field yet

		try { // ---

		if (FD_ISSET(m_tun_fd, &m_fd_set_data)) { // data incoming on TUN - send it out to peers
			anything_happened=true;

			auto size_read = read(m_tun_fd, buf, sizeof(buf)); // <-- read data from TUN
			_info("###### ------> TUN read " << size_read << " bytes: [" << string(buf,size_read)<<"]");
			const int data_route_ttl = 5; // we want to ask others with this TTL to route data sent actually by our programs

			this->route_tun_data_to_its_destination_top(
				e_route_method_from_me, buf, size_read,
				c_routing_manager::c_route_reason( c_haship_addr() , c_routing_manager::e_search_mode_route_own_packet),
				data_route_ttl
			); // push the tunneled data to where they belong
		}
		else if (FD_ISSET(m_sock_udp, &m_fd_set_data)) { // data incoming on peer (UDP) - will route it or send to our TUN
			anything_happened=true;

			sockaddr_in6 from_addr_raw; // peering address of peer (socket sender), raw format
			socklen_t from_addr_raw_size; // ^ size of it

			c_ip46_addr sender_pip; // peer-IP of peer who sent it

			// ***
			from_addr_raw_size = sizeof(from_addr_raw); // IN/OUT parameter to recvfrom, sending it for IN to be the address "buffer" size
			auto size_read = recvfrom(m_sock_udp, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>( & from_addr_raw), & from_addr_raw_size);
			_info("###### ======> UDP read " << size_read << " bytes: [" << string(buf,size_read)<<"]");
			// ^- reinterpret allowed by linux specs (TODO)
			// sockaddr *src_addr, socklen_t *addrlen);

			if (from_addr_raw_size == sizeof(sockaddr_in6)) { // the message arrive from IP pasted into sockaddr_in6 format
				_erro("NOT IMPLEMENTED yet - recognizing IP of ipv6 peer"); // peeripv6-TODO(r)(easy)
				// trivial
			}
			else if (from_addr_raw_size == sizeof(sockaddr_in)) { // the message arrive from IP pasted into sockaddr_in (ipv4) format
				sockaddr_in addr = * reinterpret_cast<sockaddr_in*>(& from_addr_raw); // mem-cast-TODO(p) confirm reinterpret
				sender_pip.set_ip4(addr);
			} else {
				throw std::runtime_error("Data arrived from unknown socket address type");
			}

			_info("UDP Socket read from direct sender_pip = " << sender_pip <<", size " << size_read << " bytes: " << string_as_dbg( string_as_bin(buf,size_read)).get());
			// ------------------------------------

			// parse version and command:
			if (! (size_read >= 2) ) { _warn("INVALIDA DATA, size_read="<<size_read); continue; } // !
			assert( size_read >= 2 ); // buf: reads from position 0..1 are asserted as valid now

			int proto_version = static_cast<int>( static_cast<unsigned char>(buf[0]) ); // TODO
			_assert(proto_version >= c_protocol::current_version ); // let's assume we will be backward compatible (but this will be not the case untill official stable version probably)
			c_protocol::t_proto_cmd cmd = static_cast<c_protocol::t_proto_cmd>( buf[1] );

			// recognize the peering HIP/CA (cryptoauth is TODO)
			c_haship_addr sender_hip;
			c_peering * sender_as_peering_ptr  = nullptr; // TODO(r)-security review usage of this, and is it needed
			if (! c_protocol::command_is_valid_from_unknown_peer( cmd )) {
				c_peering & sender_as_peering = find_peer_by_sender_peering_addr( sender_pip ); // warn: returned value depends on m_peer[], do not invalidate that!!!
				_info("We recognize the sender, as: " << sender_as_peering);
				sender_hip = sender_as_peering.get_hip(); // this is not yet confirmed/authenticated(!)
				sender_as_peering_ptr = & sender_as_peering; // pointer to owned-by-us m_peer[] element. But can be invalidated, use with care! TODO(r) check this TODO(r) cast style
			}
			_info("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ Command: " << cmd << " from peering ip = " << sender_pip << " -> peer HIP=" << sender_hip);

			if (cmd == c_protocol::e_proto_cmd_tunneled_data) { // [protocol] tunneled data

				static unsigned char generated_shared_key[crypto_generichash_BYTES] = {43, 124, 179, 100, 186, 41, 101, 94, 81, 131, 17,
								198, 11, 53, 71, 210, 232, 187, 135, 116, 6, 195, 175,
								233, 194, 218, 13, 180, 63, 64, 3, 11};
				static unsigned char nonce[crypto_aead_chacha20poly1305_NPUBBYTES] = {148, 231, 240, 47, 172, 96, 246, 79};
				static unsigned char additional_data[] = {1, 2, 3};
				static unsigned long long additional_data_len = 3;
				// TODO randomize this data

				std::unique_ptr<unsigned char []> decrypted_buf (new unsigned char[size_read + crypto_aead_chacha20poly1305_ABYTES]);
				unsigned long long decrypted_buf_len;

				int ttl_width=1; // the TTL heder width

				assert( size_read >= 1+2+ttl_width+1 );  // headers + anything

				assert(ttl_width==1); // we can "parse" just that now
				int requested_ttl = static_cast<char>(buf[1+2]); // the TTL of data that we are asked to forward

				assert(crypto_aead_chacha20poly1305_KEYBYTES <= crypto_generichash_BYTES);

				// reinterpret the char from IO as unsigned-char as wanted by crypto code
				unsigned char * ciphertext_buf = reinterpret_cast<unsigned char*>( buf ) + 2 + ttl_width; // TODO calculate depending on version, command, ...
				long long ciphertext_buf_len = size_read - 2; // TODO 2 = header size
				assert( ciphertext_buf_len >= 1 );

				int r = crypto_aead_chacha20poly1305_decrypt(
					decrypted_buf.get(), & decrypted_buf_len,
					nullptr,
					ciphertext_buf, ciphertext_buf_len,
					additional_data, additional_data_len,
					nonce, generated_shared_key);
				if (r == -1) {
					_warn("crypto verification fails");
					continue; // skip this packet (main loop)
				}

				// TODO(r) factor out "reinterpret_cast<char*>(decrypted_buf.get()), decrypted_buf_len"

				// reinterpret for debug
				_info("UDP received, with cleartext:" << decrypted_buf_len << " bytes: [" << string( reinterpret_cast<char*>(decrypted_buf.get()), decrypted_buf_len)<<"]" );

				// can't wait till C++17 then with http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/p0144r0.pdf
				// auto { src_hip, dst_hip } = parse_tun_ip_src_dst(.....);
				c_haship_addr src_hip, dst_hip;
				std::tie(src_hip, dst_hip) = parse_tun_ip_src_dst(reinterpret_cast<char*>(decrypted_buf.get()), decrypted_buf_len);

				if (dst_hip == m_haship_addr) { // received data addresses to us as finall destination:
                    _info("UDP data is addressed to us as finall dst, sending it to TUN.");
                    ssize_t write_bytes = write(m_tun_fd, reinterpret_cast<char*>(decrypted_buf.get()), decrypted_buf_len); /// *** send the data into our TUN // reinterpret char-signess
                    if (write_bytes == -1) {
                       throw std::runtime_error("Fail to send UDP to TUN: write returned -1");
                    }
                }
				else
				{ // received data that is addresses to someone else
					auto data_route_ttl = requested_ttl - 1;
					const int limit_incoming_ttl = c_protocol::ttl_max_accepted;
					if (data_route_ttl > limit_incoming_ttl) {
						_info("We were requested to route (data) at high TTL (rude) by peer " << sender_hip <<  " - so reducing it.");
						data_route_ttl=limit_incoming_ttl;
					}

					_info("UDP data is addressed to someone-else as finall dst, ROUTING it, at data_route_ttl="<<data_route_ttl);
					this->route_tun_data_to_its_destination_top(
						e_route_method_default,
						reinterpret_cast<char*>(decrypted_buf.get()), decrypted_buf_len,
						c_routing_manager::c_route_reason( src_hip , c_routing_manager::e_search_mode_route_other_packet ),
						data_route_ttl
					); // push the tunneled data to where they belong // reinterpret char-signess
				}

			} // e_proto_cmd_tunneled_data
			else if (cmd == c_protocol::e_proto_cmd_public_hi) { // [protocol]
				_note("hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh --> Command HI received");
				int offset1=2; assert( size_read >= offset1);  string_as_bin cmd_data( buf+offset1 , size_read-offset1); // buf -> bin for comfortable use

				auto pos1 = 32; // [protocol] size of public key
				if (cmd_data.bytes.at(pos1)!=';') throw std::runtime_error("Invalid protocol format, missing coma"); // [protocol]
				string_as_bin bin_his_pubkey( cmd_data.bytes.substr(0,pos1) );
				_info("We received pubkey=" << string_as_hex( bin_his_pubkey ) );
				t_peering_reference his_ref( sender_pip , string_as_bin( bin_his_pubkey ) );
				add_peer( his_ref );
			}
			else if (cmd == c_protocol::e_proto_cmd_findhip_query) { // [protocol]
				// [protocol] for search query - format is: HIP_BINARY;TTL_BINARY;
				int offset1=2; assert( size_read >= offset1);  string_as_bin cmd_data( buf+offset1 , size_read-offset1); // buf -> bin for comfortable use

				auto pos1 = cmd_data.bytes.find_first_of(';',offset1); // [protocol] size of HIP is dynamic  TODO(r)-ERROR XXX ';' is not escaped! will cause mistaken protocol errors
				decltype (pos1) size_hip = g_haship_addr_size; // possible size of HIP if ipv6
				if ((pos1==string::npos) || (pos1 != size_hip)) throw std::runtime_error("Invalid protocol format, wrong size of HIP field");

				string_as_bin bin_hip( cmd_data.bytes.substr(0,pos1) );
				c_haship_addr requested_hip( c_haship_addr::tag_constr_by_addr_bin(), bin_hip );

				string_as_bin bin_ttl( cmd_data.bytes.substr(pos1+1,1) );
				int requested_ttl = static_cast<int>( bin_ttl.bytes.at(0) ); // char to integer

				auto data_route_ttl = requested_ttl - 1;
				const int limit_incoming_ttl = c_protocol::ttl_max_accepted;
				if (data_route_ttl > limit_incoming_ttl) {
					_info("We were requested to route (help search route) at high TTL (rude) by peer " << sender_hip <<  " - so reducing it.");
					data_route_ttl=limit_incoming_ttl;
                    UNUSED(data_route_ttl); // TODO is it should be used?
                }

				_info("We received request for HIP=" << string_as_hex( bin_hip ) << " = " << requested_hip << " and TTL=" << requested_ttl );
				if (requested_ttl < 1) {
					_info("Too low TTL, dropping the request");
				} else {
					c_routing_manager::c_route_reason reason( sender_hip , c_routing_manager::e_search_mode_help_find );
					try {
						_mark("Searching for the route he asks about");
						const auto & route = m_routing_manager.get_route_or_maybe_search(*this, requested_hip , reason , true, requested_ttl - 1);
						_note("We found the route thas he asks about, as: " << route);

						// [protocol] "TTL;HIP_OF_GOAL;COST;" e.g.: (but in binary) "5;fd42...5812;1;"
						const int reply_ttl = requested_ttl; // will reply as much as needed
						unsigned char reply_ttl_byte = static_cast<unsigned char>( reply_ttl );
						assert( reply_ttl_byte == reply_ttl );
						cmd_data.bytes += reply_ttl_byte; // TODO just 1 byte, fix serialization. https://h.mantis.antinet.org/view.php?id=59
						cmd_data.bytes += ';';

						cmd_data.bytes += string_as_bin( requested_hip ).bytes; // the address of goal
						cmd_data.bytes += ';';

						auto cost_a = route.get_cost();
						unsigned char cost_byte = static_cast<unsigned char>( cost_a );
						assert( cost_byte == cost_a );
						cmd_data.bytes += cost_byte; // TODO just 1 byte, fix serialization. https://h.mantis.antinet.org/view.php?id=59
						cmd_data.bytes += ';'; // the cost

						_info("Will send data to sender_as_peering_ptr=" << sender_as_peering_ptr);
						auto peer_udp = dynamic_cast<c_peering_udp*>( sender_as_peering_ptr ); // upcast to UDP peer derived
						peer_udp->send_data_udp_cmd(c_protocol::e_proto_cmd_findhip_reply, cmd_data, m_sock_udp); // <---
						_note("Send the route reply");
					} catch(...) {
						_info("Can not yet reply to that route query.");
						// a background should be running in background usually
					}
				}

			}
			else if (cmd == c_protocol::e_proto_cmd_findhip_reply) { // [protocol]
				// [protocol] "TTL;HIP_OF_GOAL;COST;" e.g.: (but in binary) "5;fd42...5812;1;"
				int offset1=2; assert( size_read >= offset1);  string_as_bin cmd_data( buf+offset1 , size_read-offset1); // buf -> bin for comfortable use

				auto pos1 = cmd_data.bytes.find_first_of(';',offset1); // [protocol] size of HIP is dynamic  TODO(r)-ERROR XXX ';' is not escaped! will cause mistaken protocol errors
				decltype (pos1) size_hip = g_haship_addr_size; // possible size of HIP if ipv6
				if ((pos1==string::npos) || (pos1 != size_hip)) throw std::runtime_error("Invalid protocol format, wrong size of HIP field");

				string_as_bin bin_hip( cmd_data.bytes.substr(0,pos1) );
				c_haship_addr goal_hip( c_haship_addr::tag_constr_by_addr_bin(), bin_hip );

				string_as_bin bin_ttl( cmd_data.bytes.substr(pos1+1,1) );
				int requested_ttl = static_cast<int>( bin_ttl.bytes.at(0) ); // char to integer

				auto data_route_ttl = requested_ttl - 1;
				const int limit_incoming_ttl = c_protocol::ttl_max_accepted;
				if (data_route_ttl > limit_incoming_ttl) {
					_info("Got command at high TTL (rude) by peer " << sender_hip <<  " - so reducing it.");
					data_route_ttl=limit_incoming_ttl;
				}

				_info("We received REPLY for routing search for HIP=" << string_as_hex( bin_hip ) << " = " << goal_hip << " and TTL=" << requested_ttl );
				if (requested_ttl < 1) {
					_info("Too low TTL, dropping the request");
				} else {
				}
			}
			else {
				_warn("??????????????????? Unknown protocol command, cmd="<<cmd);
				continue; // skip this packet (main loop)
			}
			// ------------------------------------

		}
		else _info("Idle. " << node_title_bar);

		}
		catch (std::exception &e) {
			_warn("### !!! ### Parsing network data caused an exception: " << e.what());
		}

// stats-TODO(r) counters
//		int sent=0;
//		counter.tick(sent, std::cout);
//		counter_big.tick(sent, std::cout);
	}
}

void c_tunserver::run() {
	std::cout << "Stating the TUN router." << std::endl;
	prepare_socket();
	event_loop();
}

// ------------------------------------------------------------------

namespace developer_tests {

string make_pubkey_for_peer_nr(int peer_nr) {
	string peer_pub = string("4a4b4c") + string("4") + string(1, char('0' + peer_nr)  );
	return peer_pub;
}

// list of peers that exist in our test-world:
struct t_peer_cmdline_ref {
	string ip;
	string pubkey;
	string privkey; ///< just in the test world. here we have knowledge of peer's secret key
};

bool wip_galaxy_route_star(boost::program_options::variables_map & argm) {
	namespace po = boost::program_options;
	const int node_nr = argm["develnum"].as<int>();  assert( (node_nr>=1) && (node_nr<=254) );
	std::cerr << "Running in developer mode - as node_nr=" << node_nr << std::endl;
	// string peer_ip = string("192.168.") + std::to_string(node_nr) + string(".62");
	int peer_nr = node_nr==1 ? 2 : 1;
	string peer_pub = make_pubkey_for_peer_nr( peer_nr );
	string peer_ip = string("192.168.") + std::to_string( peer_nr  ) + string(".62"); // each connect to node .1., except the node 1 that connects to .2.

	_mark("Developer: adding peer with arguments: ip=" << peer_ip << " pub=" << peer_pub );
	// argm.insert(std::make_pair("K", po::variable_value( int(node_nr) , false )));
	argm.insert(std::make_pair("peerip", po::variable_value( peer_ip , false )));
	argm.at("peerpub") = po::variable_value( peer_pub , false );
	argm.at("mypub") = po::variable_value( make_pubkey_for_peer_nr(node_nr)  , false );
	argm.at("myname") = po::variable_value( "testnode-" + std::to_string(node_nr) , false );
	return true; // continue the test
}

bool wip_galaxy_route_doublestar(boost::program_options::variables_map & argm) {
	namespace po = boost::program_options;
	const int my_nr = argm["develnum"].as<int>();  assert( (my_nr>=1) && (my_nr<=254) ); // number of my node
	std::cerr << "Running in developer mode - as my_nr=" << my_nr << std::endl;

	// --- define the test world ---
	map< int , t_peer_cmdline_ref > peer_to_ref; // for given peer-number - the properties of said peer as seen by us (pubkey, ip - things given on the command line)
	for (int nr=1; nr<20; ++nr) { peer_to_ref[nr] = { string("192.168.") + std::to_string( nr ) + string(".62") , string("cafe") + std::to_string(nr) ,
		string("deadbeef999fff") + std::to_string(nr) };	}

	// pre-generate example test EC DH keypairs:
	peer_to_ref[1].pubkey = "3992967d946aee767b2ed018a6e1fc394f87bd5bfebd9ea7728edcf421d09471";
	peer_to_ref[1].privkey = "b98252fdc886680181fccd9b3c10338c04c5288477eeb40755789527eab3ba47";
	peer_to_ref[2].pubkey = "4491bfdafea313d1b354e0d993028f5e2a0a8119cc634226e5581db554d5283e";
	peer_to_ref[2].privkey = "bd48ab0e511fd5135134b9fb27491f3fdc344b29b8d8e7ce1b064d7946e48944";
	peer_to_ref[3].pubkey = "237e7a5224a8a58a0d264733380c4f3fba1f91482542afb269f382357c290445";
	peer_to_ref[3].privkey = "1bfb4bd0ac720276565f67798d069f7f4166076c6a37788ad21bae054f1b67c7";
	peer_to_ref[4].pubkey = "e27f2df89219841e0f930f7fbe000424bfbadabceb48eda2ab4521b5ce00b15c";
	peer_to_ref[4].privkey = "d73257edbfbf9200349bdc87bbc0f76f213d106f83fc027240e70c23a0f2f693";
	peer_to_ref[5].pubkey = "2cf0ab828ab1642f5fdcb8d197677f431d78fccd40d37400e1e6c51321512e66";
	peer_to_ref[5].privkey = "5d0dda56f336668e95816ccc4887c7ba23c1d14167918275e2bf76784a3ee702";
	peer_to_ref[6].pubkey = "26f4c825bcc045d7cb3ad6946d414c8ca1cbeaa3cd4738494e5308dd0d1cc053";
	peer_to_ref[6].privkey = "6c94c735dd0cfb862f991f05e3193e70b754650a5b4c998e68eb8bd1f43a15aa";
	peer_to_ref[7].pubkey = "a2047b24dfb02397a9354cc125eb9c2119a24b33c0c706f28bb184eeae064902";
	peer_to_ref[7].privkey = "2401f2be12ace34cfb221c168a7868d1d9dfe931f61feb8930799bb27fd5a253";
	// 2e83c0963e497c95bcd0bbc94b58b0c66b4c113b84fdd7587ca18e326a35c84c
	// 12fed56a2ffee2b0e3a51689ecb4048adfa4f474d31e9180d113f50fe140f5c3

	// list of connections in our test-world:
	map< int , vector<int> > peer_to_peer; // for given peer that we will create: list of his peer-number(s) that he peers into
	peer_to_peer[1] = vector<int>{ 2 , 3 };
	peer_to_peer[2] = vector<int>{ 4 , 5 };
	peer_to_peer[3] = vector<int>{ 6 , 7 };
	peer_to_peer[4] = vector<int>{ };
	peer_to_peer[5] = vector<int>{ };
	peer_to_peer[6] = vector<int>{ };
	peer_to_peer[7] = vector<int>{ };

	for (int peer_nr : peer_to_peer.at(my_nr)) { // for me, add the --peer refrence of all peers that I should peer into:
		_info(peer_nr);
		string peer_pub = peer_to_ref.at(peer_nr).pubkey;
		string peer_ip = peer_to_ref.at(peer_nr).ip;
		string peerref = peer_ip + "-" + peer_pub;
		_mark("Developer: adding peerref:" << peerref);

		vector<string> old_peer;
		try {
			old_peer = argm["peer"].as<vector<string>>();
			old_peer.push_back(peerref);
			argm.at("peer") = po::variable_value( old_peer , false );
		} catch(boost::bad_any_cast) {
			old_peer.push_back(peerref);
			argm.insert( std::make_pair("peer" , po::variable_value( old_peer , false )) );
		}
	}

	_info("Adding my keys command line");
	argm.at("mypub") = po::variable_value( peer_to_ref.at(my_nr).pubkey  , false );
	argm.at("mypriv") = po::variable_value( peer_to_ref.at(my_nr).privkey  , false );
	argm.at("myname") = po::variable_value( "testnode-" + std::to_string(my_nr) , false );

	_note("Done dev setup");
	return true;
}



} // namespace developer_tests

/*** 
@brief Run the main developer test in this code version (e.g. on this code branch / git branch)
@param argm - map with program options, it CAN BE MODIFIED here, e.g. the test can be to set some options and let the program continue
@return false if the program should quit after this test
*/
bool run_mode_developer(boost::program_options::variables_map & argm) { 
	std::cerr << "Running in developer mode. " << std::endl;

	// test_trivialserialize();  return false;
	antinet_crypto::test_crypto();  return false;

	return developer_tests::wip_galaxy_route_doublestar(argm);
}

int main(int argc, char **argv) {
	std::cerr << std::endl << disclaimer << std::endl << std::endl;

	{
		std::cerr<<"Startig lib sodium..."<<std::endl;
		ecdh_ChaCha20_Poly1305::init();
	}


/*	c_ip46_addr addr;
	std::cout << addr << std::endl;
	struct sockaddr_in sa;
	inet_pton(AF_INET, "192.0.2.33", &(sa.sin_addr));
	sa.sin_family = AF_INET;
	addr.set_ip4(sa);
	std::cout << addr << std::endl;

	struct sockaddr_in6 sa6;
	inet_pton(AF_INET6, "fc72:aa65:c5c2:4a2d:54e:7947:b671:e00c", &(sa6.sin6_addr));
	sa6.sin6_family = AF_INET6;
	addr.set_ip6(sa6);
	std::cout << addr << std::endl;
*/
	c_tunserver myserver;
	try {
		namespace po = boost::program_options;
		po::options_description desc("Options");
		desc.add_options()
			("help", "Print help messages")
			("devel","Test: used by developer to run current test")
			("develnum", po::value<int>()->default_value(1), "Test: used by developer to set current node number (makes sense with option --devel)")
			// ("K", po::value<int>()->required(), "number that sets your virtual IP address for now, 0-255")
			("myname", po::value<std::string>()->default_value("galaxy") , "a readable name of your node (e.g. for debug)")
			("mypub", po::value<std::string>()->default_value("") , "your public key (give any string, not yet used)")
			("mypriv", po::value<std::string>()->default_value(""), "your PRIVATE key (give any string, not yet used - of course this is just for tests)")
			//("peerip", po::value<std::vector<std::string>>()->required(), "IP over existing networking to connect to your peer")
			//("peerpub", po::value<std::vector<std::string>>()->multitoken(), "public key of your peer")
			("peer", po::value<std::vector<std::string>>()->multitoken(), "Adding entire peer reference, in syntax like ip-pub. Can be give more then once, for multiple peers.")
			;

		po::variables_map argm;
		try {
			po::store(po::parse_command_line(argc, argv, desc), argm);
			cout << "devel" << endl;
			if (argm.count("devel")) {
				try {
					bool should_continue = run_mode_developer(argm);
					if (!should_continue) return 0;
				}
				catch(std::exception& e) {
				    std::cerr << "Unhandled Exception reached the top of main: (in DEVELOPER MODE)" << e.what() << ", application will now exit" << std::endl;
						return 0; // no error for developer mode
				}
			}
			// argm now can contain options added/modified by developer mode
			po::notify(argm);


			if (argm.count("help")) {
				std::cout << desc;
				return 0;
			}

			_info("Configuring my own reference (keys):");
			myserver.configure_mykey_from_string(
				argm["mypub"].as<std::string>() ,
				argm["mypriv"].as<std::string>()
			);

			myserver.set_my_name( argm["myname"].as<string>() );

			_info("Configuring my peers references (keys):");
			vector<string> peers_cmdline;
			try { peers_cmdline = argm["peer"].as<vector<string>>(); } catch(...) { }
			for (const string & peer_ref : peers_cmdline) {
				myserver.add_peer_simplestring( peer_ref );
			}
		}
		catch(po::error& e) {
			std::cerr << "ERROR: " << e.what() << std::endl << std::endl;
			std::cerr << desc << std::endl;
			return 1;
		}
	}
	catch(std::exception& e) {
		    std::cerr << "Unhandled Exception reached the top of main: "
				<< e.what() << ", application will now exit" << std::endl;
		return 2;
	}

	myserver.run();
}

