$ErrorActionPreference = "Stop"
python -c @"
import re
with open('vendor/mlspp/src/key_schedule.cpp', 'r') as f:
    content = f.read()
# Remove ContentAAD struct definition
content = re.sub(r'// struct \{\n//     opaque group_id<0\.\.255>;\n//     uint64 epoch;\n//     ContentType content_type;\n//     opaque authenticated_data<0\.\.2\^32-1>;\n// \} ContentAAD;\nstruct ContentAAD\n\{\n  const bytes& group_id;\n  const epoch_t epoch;\n  const ContentType content_type;\n  const bytes& authenticated_data;\n\n  TLS_SERIALIZABLE\(group_id, epoch, content_type, authenticated_data\)\n\};\n\n', '', content, flags=re.MULTILINE)
# Remove ConfirmedTranscriptHashInput struct
content = re.sub(r'// struct \{\n//     WireFormat wire_format;\n//     GroupContent content; // with content\.content_type == commit\n//     opaque signature<V>;\n// \} ConfirmedTranscriptHashInput;\nstruct ConfirmedTranscriptHashInput\n\{\n  WireFormat wire_format;\n  const GroupContent& content;\n  const bytes& signature;\n\n  TLS_SERIALIZABLE\(wire_format, content, signature\)\n\};\n\n', '', content, flags=re.MULTILINE)
# Remove InterimTranscriptHashInput struct
content = re.sub(r'// struct \{\n//     MAC confirmation_tag;\n// \} InterimTranscriptHashInput;\nstruct InterimTranscriptHashInput\n\{\n  bytes confirmation_tag;\n\n  TLS_SERIALIZABLE\(confirmation_tag\)\n\};\n\n', '', content, flags=re.MULTILINE)
with open('vendor/mlspp/src/key_schedule.cpp', 'w') as f:
    f.write(content)
print('OK')
"@