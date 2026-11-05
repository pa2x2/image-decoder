# Native code finds these classes, members and constructors by name.
-keep class tachiyomi.decoder.NativeImageDecoder { *; }
-keep class tachiyomi.decoder.NativeImageDecoder$Companion { *; }
-keep interface tachiyomi.decoder.PixelAllocator { *; }
-keep class * implements tachiyomi.decoder.PixelAllocator { *** allocate(int, int); }
-keep class tachiyomi.decoder.ImageType { *; }
-keep class tachiyomi.decoder.Format { *; }
-keep class tachiyomi.decoder.Format$Companion { *; }
-keep class tachiyomi.decoder.incremental.** { *; }
