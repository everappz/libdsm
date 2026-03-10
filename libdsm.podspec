Pod::Spec.new do |s|
  s.name             = 'libdsm'
  s.version          = '0.4.0'
  s.summary          = 'SMB/CIFS client library built on top of libtasn1.'
  s.description      = <<-DESC
    libdsm is a SMB/CIFS client library providing NetBIOS name service
    discovery and SMB file sharing protocol support. It allows applications
    to browse, read, and write files on SMB/CIFS network shares.
  DESC

  s.homepage         = 'https://github.com/everappz/libdsm'
  s.license          = { :type => 'LGPL-2.1', :file => 'COPYING' }
  s.author           = { 'everappz' => 'https://github.com/everappz' }
  s.source           = { :git => 'https://github.com/everappz/libdsm.git', :tag => s.version.to_s }

  s.ios.deployment_target     = '12.0'
  s.osx.deployment_target     = '10.15'
  s.tvos.deployment_target    = '9.0'
  s.watchos.deployment_target = '2.0'

  s.source_files = [
    'src/*.{c,h}',
    'contrib/**/*.{c,h}',
    'compat/*.{c,h}',
    'include/**/*.h',
    'xcode/extra/*.{c,h}',
    'xcode/config.h',
    'config.h'
  ]

  s.exclude_files = [
    'compat/strlcpy.c'
  ]

  s.public_header_files = [
    'include/bdsm.h',
    'include/bdsm/*.h',
    'src/*.h'
  ]

  s.header_mappings_dir = '.'
  s.header_dir          = 'libdsm'

  s.pod_target_xcconfig = {
    'HEADER_SEARCH_PATHS'          => '"${PODS_TARGET_SRCROOT}/include" "${PODS_TARGET_SRCROOT}/src" "${PODS_TARGET_SRCROOT}/contrib/mdx" "${PODS_TARGET_SRCROOT}/contrib/rc4" "${PODS_TARGET_SRCROOT}/contrib/spnego" "${PODS_TARGET_SRCROOT}/compat" "${PODS_TARGET_SRCROOT}" "${PODS_ROOT}/Headers/Public/libtasn1"',
    'GCC_PREPROCESSOR_DEFINITIONS' => 'HAVE_CONFIG_H=1',
    'GCC_C_LANGUAGE_STANDARD'      => 'gnu99'
  }

  s.compiler_flags = '-DHAVE_CONFIG_H=1'

  s.libraries    = 'iconv'
  s.requires_arc = false

  s.dependency 'libtasn1', '~> 4.18'
end
