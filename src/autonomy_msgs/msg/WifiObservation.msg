std_msgs/Header header

# Each listed access point owns one row in features. Values are normalized
# with the statistics stored for the trained model before publication.
uint16 feature_count
uint16[<=64] access_point_indices
float32[<=1024] features
