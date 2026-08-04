# Security Statement

## Recommendations for Running Users
For security considerations, it is not recommended to execute any commands using administrator accounts such as root. Follow the principle of least privilege.

## File Permission Control
- Users are advised to set the system umask value to 0027 or higher on hosts (including physical machines) and containers. This ensures newly created directories have a maximum default permission of 750 and newly created files have a maximum default permission of 640.
- Users shall implement proper permission control and other security measures for sensitive content including personal private data, commercial assets, source files, and various files generated during code development. Examples include permission management for the project installation directory and public input data files. Refer to [A - Recommended Maximum Permissions for Files/Directories in Different Scenarios](https://gitcode.com/cann/asc-devkit/blob/master/SECURITY.md#a-文件夹各场景权限管控推荐最大值) for suggested permission settings.
- Users shall enforce permission control during installation and usage. Configure permissions with reference to the guidelines in [A - Recommended Maximum Permissions for Files/Directories in Different Scenarios](https://gitcode.com/cann/asc-devkit/blob/master/SECURITY.md#a-文件夹各场景权限管控推荐最大值).

## Build Security Statement
If you compile and install this project from source code, intermediate files will be generated during compilation. After compilation completes, it is recommended to apply proper permission restrictions on these intermediate files to ensure file security.

## Runtime Security Statement
- The process will exit and print error logs upon runtime exceptions. Locate the root cause according to the error prompts.

## Public Network Address Disclosure
Public network addresses contained within the project source code are listed below:

| Type | Open Source Repository URL | File Name | Public IP / Public URL / Domain / Email / Archive URL | Purpose Description |
|:----:|:--------------------------:|:---------:|:-----------------------------------------------------:|:--------------------|
| Dependency | N/A | cmake/third_party/gtest.cmake | https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz | Download googletest source code from GitCode as compilation dependency |

## Vulnerability Handling Mechanism
[Vulnerability Management](https://gitcode.com/cann/community/blob/master/security/security.md)

## Appendix
### A - Recommended Maximum Permissions for Files/Directories in Different Scenarios
| Category | Recommended Maximum Linux Permission |
| ---- | ---- |
| User home directory | 750 (rwxr-x---) |
| Program files (scripts, libraries, etc.) | 550 (r-xr-x---) |
| Directory for program files | 550 (r-xr-x---) |
| Configuration files | 640 (rw-r-----) |
| Directory for configuration files | 750 (rwxr-x---) |
| Archived / completed log files | 440 (r--r-----) |
| Active log files (in writing) | 640 (rw-r-----) |
| Directory for log files | 750 (rwxr-x---) |
| Debug files | 640 (rw-r-----) |
| Directory for debug files | 750 (rwxr-x---) |
| Temporary file directory | 750 (rwxr-x---) |
| Maintenance & upgrade file directory | 770 (rwxrwx---) |
| Business data files | 640 (rw-r-----) |
| Directory for business data files | 750 (rwxr-x---) |
| Directory for key components, private keys, certificates and encrypted files | 700 (rwx------) |
| Key components, private keys, certificates and encrypted data | 600 (rw-------) |
| Encryption/decryption interfaces and scripts | 500 (r-x------) |
