Pod::Spec.new do |s|
  s.name             = 'libtasn1'
  s.version          = '4.18.0'
  s.summary          = 'GNU Libtasn1 - ASN.1 library for DER/BER encoding and decoding.'
  s.description      = <<-DESC
    GNU Libtasn1 is a portable C library that encodes and decodes
    DER/BER data following an ASN.1 schema. It is used by GnuTLS
    and other security-related projects.
  DESC

  s.homepage         = 'https://github.com/leshkoapps/libdsm'
  s.license          = { :type => 'LGPL-2.1', :file => 'LICENSE' }
  s.author           = { 'leshkoapps' => 'https://github.com/leshkoapps' }
  s.source           = { :git => 'https://github.com/leshkoapps/libdsm.git', :tag => s.version.to_s }

  s.ios.deployment_target     = '12.0'
  s.osx.deployment_target     = '10.15'
  s.tvos.deployment_target    = '9.0'
  s.watchos.deployment_target = '2.0'

  s.source_files = [
    'lib/*.{c,h}',
    'lib/gllib/*.{c,h}',
    'config.h'
  ]

  s.public_header_files = 'lib/libtasn1.h'

  s.header_mappings_dir = '.'
  s.header_dir          = 'libtasn1'

  s.pod_target_xcconfig = {
    'HEADER_SEARCH_PATHS'        => '"${PODS_TARGET_SRCROOT}/lib" "${PODS_TARGET_SRCROOT}"',
    'GCC_PREPROCESSOR_DEFINITIONS' => 'HAVE_CONFIG_H=1',
    'GCC_C_LANGUAGE_STANDARD'    => 'gnu99',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'gnu++0x',
    'CLANG_CXX_LIBRARY'          => 'libc++'
  }

  s.compiler_flags = '-DHAVE_CONFIG_H=1'

  s.libraries    = 'c++'
  s.requires_arc = false
end
