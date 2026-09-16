# Security Statement

## Recommendations for Running Users

For security considerations, it is not recommended to execute any commands using administrator accounts such as root. Follow the principle of least privilege.

## File Permission Control

- Users are advised to set the system umask value to 0027 or higher on hosts (including physical machines) and containers. This ensures newly created directories have a maximum default permission of 750 and newly created files have a maximum default permission of 640.
- Recommend users implement permission control and other security measures for sensitive content such as personal privacy data, business assets, source files, and various files saved during code development. For example, permission control for this project installation directory, public data file permission control. Refer to [A-File (Folder) Permission Control Recommended Maximum Values for Various Scenarios](SECURITY_en.md#a-file-folder-permission-control-recommended-maximum-values-for-various-scenarios) for recommended permissions.
- Users need to implement permission control during installation and usage. Refer to [A-File (Folder) Permission Control Recommended Maximum Values for Various Scenarios](SECURITY_en.md#a-file-folder-permission-control-recommended-maximum-values-for-various-scenarios) for file permission reference settings.

## Build Security Statement

If you compile and install this project from source code, intermediate files will be generated during compilation. After compilation completes, it is recommended to apply proper permission restrictions on these intermediate files to ensure file security.

## Runtime Security Statement

- The process will exit and print error logs upon runtime exceptions. Locate the root cause according to the error prompts.

## Public Network Address Disclosure

Public network addresses contained within the project source code are listed below:

| Type | Open Source Repository URL | File Name | Public IP / Public URL / Domain / Email / Archive URL | Purpose Description |
|:----:|:--------------------------:|:---------:|:-----------------------------------------------------:|:--------------------|
| Dependency | N/A | cmake/third_party/gtest.cmake | <https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz> | Download googletest source code from GitCode as compilation dependency |

## Vulnerability Handling Mechanism

[Vulnerability Management](https://gitcode.com/cann/community/blob/master/security/security.md)

## Appendix

### A-File (Folder) Permission Control Recommended Maximum Values for Various Scenarios

| Type | Linux Permission Reference Maximum Value |
| -------------- | ---------------  |
| User home directory | 750 (rwxr-x---) |
| Program files (including script files, library files, and so on) | 550 (r-xr-x---) |
| Program file directory | 550 (r-xr-x---) |
| Configuration files | 640 (rw-r-----) |
| Configuration file directory | 750 (rwxr-x---) |
| Log files (completed recording or archived) | 440 (r--r-----) |
| Log files (currently recording) | 640 (rw-r-----) |
| Log file directory | 750 (rwxr-x---) |
| Debug files | 640 (rw-r-----) |
| Debug file directory | 750 (rwxr-x---) |
| Temporary file directory | 750 (rwxr-x---) |
| Maintenance upgrade file directory | 770 (rwxrwx---) |
| Business data files | 640 (rw-r-----) |
| Business data file directory | 750 (rwxr-x---) |
| Key component, private key, certificate, ciphertext file directory | 700 (rwx------) |
| Key component, private key, certificate, encrypted ciphertext | 600 (rw-------) |
| Encryption/decryption interfaces, encryption/decryption scripts | 500 (r-x------)
