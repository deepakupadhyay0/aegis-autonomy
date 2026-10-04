std_msgs/Header header

string<=128 incident_id
string<=32 as_of
uint64 sequence
uint8 source_level
string<=64 source_node
string<=128 source_evidence_id
string<=256 fault
# Unverified model prose. The publisher limits it to maximum_response_bytes.
string<=1048576 analysis
