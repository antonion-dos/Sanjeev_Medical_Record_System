#pragma once

#include "block.hpp"
#include <string>
#include <vector>
#include <map>

namespace Sanjeev {

struct AuthorityNode {
    Address address{};
    std::string name;
    std::string public_key_pem;
    bool is_active = true;
};

class PoAConsensus {
public:
    PoAConsensus() = default;

    void register_authority(const Address& address, const std::string& name, const std::string& public_key_pem);
    void revoke_authority(const Address& address);
    bool is_authorized(const Address& address) const;
    const AuthorityNode* get_authority(const Address& address) const;
    std::vector<AuthorityNode> get_all_authorities() const;

    bool sign_block(Block& block, const std::string& private_key_pem, const Address& authority_address, const std::string& authority_name) const;
    bool validate_block(const Block& block, const Block* prev_block) const;

private:
    std::map<Address, AuthorityNode> authorities_;
};

} // namespace Sanjeev
