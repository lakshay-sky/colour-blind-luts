//
//  File:       GenCBLut.cpp
//
//  Function:   Utilities for colour-blind modelling and LUT construction

#define _CRT_SECURE_NO_WARNINGS

#include "CBLuts.h"

#include "ColourMaps.h"

#include "stb_image_mini.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <fstream>
#include <iomanip>
#include <ctime>

#ifdef _MSC_VER
    #define strlcpy(d, s, ds) strcpy_s(d, ds, s)
#endif

using namespace CBLut;

void SaveLUTAsCSV(const RGBA32 rgbLUT[kLUTSize][kLUTSize][kLUTSize], const char* filename)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        printf("Failed to open file for writing.\n");
        return;
    }
    printf("opened file");
    // Get the current date for the LUT_DATE field
    time_t now = time(0);
    tm* ltm = localtime(&now);
    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", 1900 + ltm->tm_year, 1 + ltm->tm_mon, ltm->tm_mday);

    // Write the header
    file << "# CalMAN ,,,,,\n";
    file << "# LUT_DATE: " << dateStr << ",,,,\n";
    file << "# LUT_3D_SIZE: " << kLUTSize << ",,,,\n";
    file << "# LUT_BIT_DEPTH: 12,,,,\n";
    file << "# LUT_ORDER: RGB,,,,\n";

    // Write normalized LUT values
    for (int r = 0; r < kLUTSize; ++r)
    {
        for (int g = 0; g < kLUTSize; ++g)
        {
            for (int b = 0; b < kLUTSize; ++b)
            {
                RGBA32 color = rgbLUT[r][g][b];
                // Normalize the RGB values to [0.0, 1.0]
                float normalizedR = color.c[0] / 255.0f;
                float normalizedG = color.c[1] / 255.0f;
                float normalizedB = color.c[2] / 255.0f;

                // Write the LUT entry
                file << r << "," << g << "," << b << ",";
                file << std::fixed << std::setprecision(6)
                     << normalizedR << "," << normalizedG << "," << normalizedB << "\n";
            }
        }
    }

    file.close();
    printf("LUT saved to %s\n", filename);
}


namespace
{
    inline Vec3f operator+(Vec3f a, Vec3f b) { return { a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec3f operator-(Vec3f a, Vec3f b) { return { a.x - b.x, a.y - b.y, a.z - b.z}; }
    
    inline Vec3f& operator*=(Vec3f& v, float s) { v.x *= s; v.y *= s; v.z *= s; return v; }
    
    inline float dot      (Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3f operator*(Vec3f a, Vec3f b) { return { a.x * b.x, a.y * b.y, a.z * b.z}; }

    inline Vec3f operator * (const Mat3f& m, const Vec3f& v)
    {
        return Vec3f
        {
            dot(m.x, v),
            dot(m.y, v),
            dot(m.z, v),
        };
    }

    inline Vec3f ClampUnit(Vec3f c)
    {
        return { 
        c.x < 0.0f ? 0.0f : c.x > 1.0f ? 1.0f : c.x, 
        c.y < 0.0f ? 0.0f : c.y > 1.0f ? 1.0f : c.y, 
        c.z < 0.0f ? 0.0f : c.z > 1.0f ? 1.0f : c.z };
    }

    Vec3f RGBError(Vec3f c, tLMS lmsType, float strength)
    {
        Vec3f sc = Simulate(c, lmsType, strength); 
        return c - sc;
    }
    

    template<class T> void CreateLUT(T xform, RGBA32 rgbLUT[kLUTSize][kLUTSize][kLUTSize])
    {
        printf("j");
        constexpr int scale  = 256 / kLUTSize;
        constexpr int offset = scale / 2;

        for (int i = 0; i < kLUTSize; i++)
        for (int j = 0; j < kLUTSize; j++)
        for (int k = 0; k < kLUTSize; k++)
        {
            // Vec3f c{ (k + 0.5f) / kLUTSize, (j + 0.5f) / kLUTSize, (i + 0.5f) / kLUTSize };
            RGBA32 identity = { uint8_t(k * scale + offset), uint8_t(j * scale + offset), uint8_t(i * scale + offset), 255 };

            Vec3f c = FromRGBA32u(identity);

            c = xform(c);

            rgbLUT[i][j][k] = ToRGBA32u(c);
        }
    }

    template<class T> void Transform(T xform, int n, const RGBA32 dataIn[], RGBA32 dataOut[])
    {
            printf("i");
        for (int i = 0; i < n; i++)
        {
            Vec3f c = FromRGBA32(dataIn[i]);

            c = xform(c);

            dataOut[i] = ToRGBA32(c);
        }
    }

    template<class T> inline void PerformOp(T xform, RGBA32 rgbLUT[kLUTSize][kLUTSize][kLUTSize], int n, const RGBA32 dataIn[], RGBA32 dataOut[])
    {

        if (dataOut)
        {   printf("g");
            Transform(xform, n, dataIn, dataOut);
        }
        else
        {   printf("h");
            CreateLUT(xform, rgbLUT);   
        }
    }
}


namespace
{
    // Main rgb LUT processing routines
    enum tCBType
    {
        kIdentity,

        kProtanope,
        kDeuteranope,
        kTritanope,

        kAll,
    };

    enum tImageOp 
    {
        kSimulate,
        kDaltonise,
        kCorrect,
        kDaltoniseSimulate,
        kCorrectSimulate,
    };

    void CreateCube(tImageOp op, tCBType cbType, float strength, int w, int h, const RGBA32* dataIn, const char* dataInName, bool noLUT)
    {
        if (cbType == kAll)
        {
            printf("e");
            CreateCube(op, kProtanope,   strength, w, h, dataIn, dataInName, noLUT);
            CreateCube(op, kDeuteranope, strength, w, h, dataIn, dataInName, noLUT);
            CreateCube(op, kTritanope,   strength, w, h, dataIn, dataInName, noLUT);
            return;
        };

        tLMS lmsType = kL;
        char filename[256] = "";

        if (dataIn)
        {
            printf("d");    
            snprintf(filename, sizeof(filename), "%s_", dataInName);
            
        }
        switch (cbType)
        {
        case kIdentity:
            strcat(filename, "identity");
            break;
        case kProtanope:
            strcat(filename, "protanope");
            lmsType = kL;
            break;
        case kDeuteranope:
            strcat(filename, "deuteranope");
            lmsType = kM;
            break;
        case kTritanope:
            strcat(filename, "tritanope");
            lmsType = kS;
            break;
        default:
            return;
        }

        char csvFilename[256] = "";
        snprintf(csvFilename, sizeof(csvFilename), "%s_%s_lut.csv", dataInName, (cbType == kProtanope ? "protanope" : cbType == kDeuteranope ? "deuteranope" : "tritanope"));
    
        RGBA32 rgbaLUT[kLUTSize][kLUTSize][kLUTSize];
        RGBA32* dataOut = 0;
        int n = w * h;
        
        if (noLUT && dataIn) 
        {   
            printf("k");
            dataOut = new RGBA32[n];
        }
        switch (op)
        {
        case kSimulate:
            PerformOp([lmsType, strength](Vec3f c){ return Simulate(c, lmsType, strength); }, rgbaLUT, n, dataIn, dataOut);
            strcat(filename, "_simulate");
            break;
        case kDaltonise:
            PerformOp([lmsType, strength](Vec3f c) { return Daltonise(c, lmsType, strength); }, rgbaLUT, n, dataIn, dataOut);
            strcat(filename, "_daltonise");
            break;
        case kCorrect:
            PerformOp([lmsType, strength](Vec3f c) { return Correct(c, lmsType, strength); }, rgbaLUT, n, dataIn, dataOut);
            strcat(filename, "_correct");
            break;
        case kDaltoniseSimulate:
            PerformOp([lmsType, strength](Vec3f c) { return Simulate(ClampUnit(Daltonise(c, lmsType, strength)), lmsType, strength); }, rgbaLUT, n, dataIn, dataOut);
            strcat(filename, "_simulate_daltonised");
            break;
        case kCorrectSimulate:
            PerformOp([lmsType, strength](Vec3f c) { return Simulate(ClampUnit(Correct(c, lmsType, strength)), lmsType, strength); }, rgbaLUT, n, dataIn, dataOut);
            strcat(filename, "_simulate_corrected");
            break;
        };
/*
        if (dataIn && !dataOut)
        {   
            printf("a");
            dataOut = new RGBA32[n];
            printf("LUT= \n %f",rgbaLUT);
            ApplyLUT(rgbaLUT, n, dataIn, dataOut);
        }
*/
        if (dataIn && !dataOut)
        {   
            printf("a");
            dataOut = new RGBA32[n];  // Allocate memory for the output image data.

            // Optional: Print the LUT (but printing the entire LUT can be excessive)
            // Loop through the LUT and print some values if needed for debugging.
  /*          printf("LUT values: \n");
            for (int i = 0; i < kLUTSize; ++i)
            {
                for (int j = 0; j < kLUTSize; ++j)
                {
                    for (int k = 0; k < kLUTSize; ++k)
                    {
                        RGBA32 color = rgbaLUT[i][j][k];  // Access LUT values.
                        printf("LUT[%d][%d][%d] = R:%u G:%u B:%u A:%u\n", 
                            i, j, k, color.c[0], color.c[1], color.c[2], color.c[3]);  // Print each RGBA component.
                    }
                }
            }
    */
           

            
            ApplyLUT(rgbaLUT, n, dataIn, dataOut);  // Apply the LUT to the input data.
        }

        if (dataOut)
        {   
            printf("b");
            strcat(filename, ".png");
            printf("Saving %s\n", filename);
            stbi_write_png(filename, w, h, 4, dataOut, 0);

            delete[] dataOut;
            printf("Saving LUT as CSV: %s\n", csvFilename);
            SaveLUTAsCSV(rgbaLUT, csvFilename);
        }
        else
        {
            printf("c");
            strcat(filename, "_lut.png");
            printf("Saving %s\n", filename);
            stbi_write_png(filename, kLUTSize * kLUTSize, kLUTSize, 4, rgbaLUT, 0);
            printf("Saving LUT as CSV: %s\n", csvFilename);
            SaveLUTAsCSV(rgbaLUT, csvFilename);
        }
    }
}

namespace
{
    int Help(const char* command)
    {
        printf
        (
            "%s <options> <operations>\n"
            "\n"
            "Options:\n"
            "  -h        : this help\n"
            "  -f <path> : set image to process rather than emitting lut\n"
            "  -p        : emit protanope image or lut\n"
            "  -d        : emit deuteranope image or lut\n"
            "  -t        : emit tritanope image or lut\n"
            "  -a        : emit image or lut for all the above types (default)\n"
            "  -m <str>  : specify strength of colour blindness to correct for. Default = 1 (affected channel is completely lost.)\n" 
            "\n"
            "Operations:\n"
            "  -s        : simulate given type of colour-blindness\n"
            "  -x        : daltonise (Fidaner) for given type of colour-blindness\n"
            "  -X        : daltonise for and then simulate given type of colour-blindness\n"
            "  -y        : correct for given type of colour-blindness\n"
            "  -Y        : correct for and then simulate given type of colour-blindness\n"
            "\nExample:\n"
            "  %s -f image.png -p -sxy\n"
            "      # emit simulated, daltonised, and corrected version of image.png for protanopia only.\n"
            , command, command
        );

        return 0;
    }

    void GetFileName(char* buffer, size_t bufferSize, const char* path)
    {
        const char* lastSlash = strrchr(path, '/');
        if (!lastSlash)
            lastSlash = strrchr(path, '\\');

        if (lastSlash)
            strlcpy(buffer, lastSlash + 1, bufferSize);
        else
            strlcpy(buffer, path, bufferSize);

        char* lastDot = strrchr(buffer, '.');

        if (lastDot)
            *lastDot = 0;
    }
}

int main(int argc, const char* argv[])
{
    const char* command = argv[0];
    argv++; argc--;

    if (argc == 0)
        return Help(command);

    tCBType cbType = kAll;
    int w;
    int h;
    RGBA32* dataIn = 0;
    char dataInName[256] = "unknown";
    float strength = 1.0f;
    bool noLUT = false;

    // Options
    while (argc > 0 && argv[0][0] == '-')
    {
        const char* option = argv[0] + 1;
        argv++; argc--;

        while (option[0])
        {
            switch (option[0])
            {
            case 'h':
            case '?':
                return Help(command);

            case 'f':
                if (argc <= 0)
                    return fprintf(stderr, "Expecting filename with -f\n");

                dataIn = (RGBA32*) stbi_load(argv[0], &w, &h, 0, 4);
                
                if (!dataIn)
                {
                    fprintf(stderr, "Couldn't read %s\n", argv[0]);
                    return -1;
                }

                GetFileName(dataInName, sizeof(dataInName), argv[0]);

                argv++; argc--;
                break;

            case 'm':
                if (argc <= 0)
                    return fprintf(stderr, "Expecting strength for -m <float>\n");
                strength = (float) atof(argv[0]);
                argv++; argc--;
                break;

            case 's':
                CreateCube(kSimulate,          cbType, strength, w, h, dataIn, dataInName, noLUT);
                break;

            case 'x':
                CreateCube(kDaltonise,         cbType, strength, w, h, dataIn, dataInName, noLUT);
                break;
            case 'X':
                CreateCube(kDaltoniseSimulate, cbType, strength, w, h, dataIn, dataInName, noLUT);
                break;

            case 'y':
                CreateCube(kCorrect,           cbType, strength, w, h, dataIn, dataInName, noLUT);
                break;
            case 'Y':
                CreateCube(kCorrectSimulate,   cbType, strength, w, h, dataIn, dataInName, noLUT);
                break;

            case 'p':
                cbType = kProtanope;
                break;

            case 'd':
                cbType = kDeuteranope;
                break;

            case 't':
                cbType = kTritanope;
                break;

            case 'a':
                cbType = kAll;
                break;

            }
            option++;
        }
    }

    if (dataIn)
        stbi_image_free(dataIn);

    if (argc > 0)
    {
        fprintf(stderr, "Unrecognised arguments starting with %s\n", argv[0]);
        return -1;
    }
        
    return 0;
}