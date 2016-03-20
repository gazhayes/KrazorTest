// Copyright (c) 2014-2016, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include "gtest/gtest.h"
#include "cryptonote_core/cryptonote_core.h"
#include "p2p/net_node.h"
#include "cryptonote_protocol/cryptonote_protocol_handler.h"

namespace cryptonote {
  class blockchain_storage;
}

class test_core
{
public:
  void on_synchronized(){}
  uint64_t get_current_blockchain_height() const {return 1;}
  void set_target_blockchain_height(uint64_t) {}
  bool init(const boost::program_options::variables_map& vm) {return true ;}
  bool deinit(){return true;}
  bool get_short_chain_history(std::list<crypto::hash>& ids) const { return true; }
  bool get_stat_info(cryptonote::core_stat_info& st_inf) const {return true;}
  bool have_block(const crypto::hash& id) const {return true;}
  bool get_blockchain_top(uint64_t& height, crypto::hash& top_id)const{height=0;top_id=cryptonote::null_hash;return true;}
  bool handle_incoming_tx(const cryptonote::blobdata& tx_blob, cryptonote::tx_verification_context& tvc, bool keeped_by_block, bool relaued) { return true; }
  bool handle_incoming_block(const cryptonote::blobdata& block_blob, cryptonote::block_verification_context& bvc, bool update_miner_blocktemplate = true) { return true; }
  void pause_mine(){}
  void resume_mine(){}
  bool on_idle(){return true;}
  bool find_blockchain_supplement(const std::list<crypto::hash>& qblock_ids, cryptonote::NOTIFY_RESPONSE_CHAIN_ENTRY::request& resp){return true;}
  bool handle_get_objects(cryptonote::NOTIFY_REQUEST_GET_OBJECTS::request& arg, cryptonote::NOTIFY_RESPONSE_GET_OBJECTS::request& rsp, cryptonote::cryptonote_connection_context& context){return true;}
  cryptonote::blockchain_storage &get_blockchain_storage() { throw tools::runtime_error("Called invalid member function: please never call get_blockchain_storage on the TESTING class test_core."); }
  bool get_test_drop_download() const {return true;}
  bool get_test_drop_download_height() const {return true;}
  bool prepare_handle_incoming_blocks(const std::list<cryptonote::block_complete_entry>  &blocks) { return true; }
  bool cleanup_handle_incoming_blocks(bool force_sync = false) { return true; }
  uint64_t get_target_blockchain_height() const { return 1; }
};

typedef nodetool::node_server<cryptonote::t_cryptonote_protocol_handler<test_core>> Server;

static bool is_blocked(Server &server, uint32_t ip, time_t *t = NULL)
{
  std::map<uint32_t, time_t> ips = server.get_blocked_ips();
  for (auto rec: ips)
  {
    if (rec.first == ip)
    {
      if (t)
        *t = rec.second;
      return true;
    }
  }
  return false;
}

TEST(ban, add)
{
  test_core pr_core;
  cryptonote::t_cryptonote_protocol_handler<test_core> cprotocol(pr_core, NULL);
  Server server(cprotocol);
  cprotocol.set_p2p_endpoint(&server);

  // starts empty
  ASSERT_TRUE(server.get_blocked_ips().empty());
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // add an IP
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // add the same, should not change
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // remove an unblocked IP, should not change
  ASSERT_FALSE(server.unblock_ip(MAKE_IP(1,2,3,5)));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // remove the IP, ends up empty
  ASSERT_TRUE(server.unblock_ip(MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(server.get_blocked_ips().size() == 0);
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // remove the IP from an empty list, still empty
  ASSERT_FALSE(server.unblock_ip(MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(server.get_blocked_ips().size() == 0);
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // add two for known amounts of time, they're both blocked
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4), 1));
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,5), 3));
  ASSERT_TRUE(server.get_blocked_ips().size() == 2);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,5)));
  ASSERT_TRUE(server.unblock_ip(MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(server.unblock_ip(MAKE_IP(1,2,3,5)));

  // these tests would need to call is_remote_ip_allowed, which is private
#if 0
  // after two seconds, the first IP is unblocked, but not the second yet
  sleep(2);
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,5)));

  // after two more seconds, the second IP is also unblocked
  sleep(2);
  ASSERT_TRUE(server.get_blocked_ips().size() == 0);
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,4)));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));
#endif

  // add an IP again, then re-ban for longer, then shorter
  time_t t;
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4), 2));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4), &t));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));
  ASSERT_TRUE(t >= 1);
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4), 9));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4), &t));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));
  ASSERT_TRUE(t >= 8);
  ASSERT_TRUE(server.block_ip(MAKE_IP(1,2,3,4), 5));
  ASSERT_TRUE(server.get_blocked_ips().size() == 1);
  ASSERT_TRUE(is_blocked(server,MAKE_IP(1,2,3,4), &t));
  ASSERT_FALSE(is_blocked(server,MAKE_IP(1,2,3,5)));
  ASSERT_TRUE(t >= 4);
}

