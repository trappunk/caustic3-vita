#ifndef CAUSTIC_APK_EXTRACT_H
#define CAUSTIC_APK_EXTRACT_H

int extract_caustic_demo_assets(const char *apk_path, const char *destination);
int install_caustic_extra_bundle(const char *source, const char *destination,
                                 const char *marker, const char *label,
                                 int preserve_existing);
int ensure_caustic_config_line(const char *path, const char *key,
                               const char *line);

#endif
