target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:
                -Wall
                -Wextra
                -Wabi
                -Wcast-align
                -Wcast-qual
                -Wdate-time
                -Wdisabled-optimization
                -Wduplicated-cond
                -Wenum-compare
                -Wformat=2
                -Wformat-signedness
                -Wframe-larger-than=10000
                -Winit-self
                -Wlarger-than=1000000
                -Wlogical-op
                -Wmissing-declarations
                -Wmissing-format-attribute
                -Wmissing-include-dirs
                -Wpacked
                -Wredundant-decls
                -Wshadow
                -Wstack-usage=10000
                -Wstrict-aliasing=3
                -Wstrict-overflow=1
                -Wsuggest-attribute=format
                -Wsuggest-attribute=noreturn
                -Wsuggest-final-methods
                -Wsuggest-final-types
                -Wswitch-enum
                -Wtrampolines
                -Wundef
                -Wunreachable-code
                -Wunused
                -Wunused-parameter
                -Wvla
                -Wwrite-strings
                $<$<COMPILE_LANGUAGE:CXX>:
                -Wconditionally-supported
                -Wctor-dtor-privacy
                -Wnoexcept
                -Wnon-virtual-dtor
                -Wold-style-cast
                -Woverloaded-virtual
                -Wsign-promo
                -Wsuggest-override
                -Wvirtual-inheritance
                -Wzero-as-null-pointer-constant
                >
                $<$<COMPILE_LANGUAGE:C>:
                -Wbad-function-cast
                -Wc++-compat
                -Wjump-misses-init
                -Wmissing-prototypes
                -Wnested-externs
                -Wold-style-definition
                -Wstrict-prototypes
                -Wunsuffixed-float-constants
                >
        >
        $<$<CXX_COMPILER_ID:Clang>:
                -Weverything
                $<$<COMPILE_LANGUAGE:CXX>:
                -Wno-c++98-compat
                -Wno-c++98-compat-pedantic
                -Wno-conversion
                -Wno-double-promotion
                -Wno-exit-time-destructors
                -Wno-float-equal
                -Wno-padded
                -Wno-weak-vtables
                >
        >
)

#-Waggregate-return
#-Wconversion
#-Wdouble-promotion
#-Werror
#-Wfatal-errors
#-Wfloat-conversion
#-Wfloat-equal
#-Winline
#-Wpadded
#-Wsign-conversion
#-Wsuggest-attribute=const
#-Wsuggest-attribute=pure
#-Wswitch-default
#-Wunsafe-loop-optimizations

#C++:
#-Weffc++
#-Wmultiple-inheritance
#-Wuseless-cast

#C:
#-Wtraditional
#-Wtraditional-conversion
