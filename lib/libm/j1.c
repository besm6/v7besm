/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * j1(x), y1(x) -- Bessel functions of the first and second kinds, order one.
 * v7 extensions, not C11; kept because v7's libm has them.
 *
 * The shape is j0.c's exactly: j1 finite everywhere, y1 a domain error for x <= 0,
 * a rational fit below x = 8 and the asymptotic form above it.  Coefficients are Hart
 * & Cheney (#6050, #6750, #7150, #6447).
 *
 * THE COEFFICIENTS ARE SCALED, for the reason j0.c gives: p1/q1 lead near 1e21 and
 * p4/q4 near 5e23, both past the largest finite value here.  p1/q1 are divided by
 * 10^11 and p4/q4 by 10^13 -- a shift of every literal's decimal exponent, the ratio
 * n/d unchanged.  Each array's factor is on its first line.  p2/q2 and p3/q3 fit and
 * are v7's outright.
 */
#include <errno.h>
#include <math.h>

static double pzero, qzero;
static double tpi = .6366197723675813430755350535e0;
static double pio4 = .7853981633974483096156608458e0;

static double p1[] = {
    /* scaled by 1e-11 */
    0.581199354001606143928050809e10,    -.6672106568924916298020941484e9,
    0.2316433580634002297931815435e8,    -.3588817569910106050743641413e6,
    0.2908795263834775409737601689e4,    -.1322983480332126453125473247e2,
    0.3413234182301700539091292655e-1,   -.4695753530642995859767162166e-4,
    0.2701122710892323414856790990e-7,
};
static double q1[] = {
    /* scaled by 1e-11 */
    0.1162398708003212287858529400e11,   0.1185770712190320999837113348e9,
    0.6092061398917521746105196863e6,    0.2081661221307607351240184229e4,
    0.5243710262167649715406728642e1,    0.1013863514358673989967045588e-1,
    0.1501793594998585505921097578e-4,   0.1606931573481487801970916749e-7,
    1.0e-11,
};
static double p2[] = {
    -.4435757816794127857114720794e7,    -.9942246505077641195658377899e7,
    -.6603373248364939109255245434e7,    -.1523529351181137383255105722e7,
    -.1098240554345934672737413139e6,    -.1611616644324610116477412898e4,
    0.0,
};
static double q2[] = {
    -.4435757816794127856828016962e7,    -.9934124389934585658967556309e7,
    -.6585339479723087072826915069e7,    -.1511809506634160881644546358e7,
    -.1072638599110382011903063867e6,    -.1455009440190496182453565068e4,
    1.0e0,
};
static double p3[] = {
    0.3322091340985722351859704442e5,    0.8514516067533570196555001171e5,
    0.6617883658127083517939992166e5,    0.1849426287322386679652009819e5,
    0.1706375429020768002061283546e4,    0.3526513384663603218592175580e2,
    0.0,
};
static double q3[] = {
    0.7087128194102874357377502472e6,    0.1819458042243997298924553839e7,
    0.1419460669603720892855755253e7,    0.4002944358226697511708610813e6,
    0.3789022974577220264142952256e5,    0.8638367769604990967475517183e3,
    1.0e0,
};
static double p4[] = {
    /* scaled by 1e-13 */
    -.9963753424306922225996744354e10,   0.2655473831434854326894248968e10,
    -.1212297555414509577913561535e9,    0.2193107339917797592111427556e7,
    -.1965887462722140658820322248e5,    0.9569930239921683481121552788e2,
    -.2580681702194450950541426399e0,    0.3639488548124002058278999428e-3,
    -.2108847540133123652824139923e-6,   0.0,
};
static double q4[] = {
    /* scaled by 1e-13 */
    0.5082067366941243245314424152e11,   0.5435310377188854170800653097e9,
    0.2954987935897148674290758119e7,    0.1082258259408819552553850180e5,
    0.2976632125647276729292742282e2,    0.6465340881265275571961681500e-1,
    0.1128686837169442121732366891e-3,   0.1563282754899580604737366452e-6,
    0.1612361029677000859332072312e-9,   1.0e-13,
};

static void asympt(double arg)
{
    double zsq, n, d;
    int i;
    zsq = 64. / (arg * arg);
    for (n = 0, d = 0, i = 6; i >= 0; i--) {
        n = n * zsq + p2[i];
        d = d * zsq + q2[i];
    }
    pzero = n / d;
    for (n = 0, d = 0, i = 6; i >= 0; i--) {
        n = n * zsq + p3[i];
        d = d * zsq + q3[i];
    }
    qzero = (8. / arg) * (n / d);
}

double j1(double arg)
{
    double xsq, n, d, x;
    int i;

    x = arg;
    if (x < 0.)
        x = -x;
    if (x > 8.) {
        asympt(x);
        n = x - 3. * pio4;
        n = sqrt(tpi / x) * (pzero * cos(n) - qzero * sin(n));
        if (arg < 0.)
            n = -n;
        return n;
    }
    xsq = x * x;
    for (n = 0, d = 0, i = 8; i >= 0; i--) {
        n = n * xsq + p1[i];
        d = d * xsq + q1[i];
    }
    return arg * n / d;
}

double y1(double arg)
{
    double xsq, n, d, x;
    int i;

    x = arg;
    if (x <= 0.) {
        errno = EDOM;
        return -HUGE_VAL;
    }
    if (x > 8.) {
        asympt(x);
        n = x - 3 * pio4;
        return sqrt(tpi / x) * (pzero * sin(n) + qzero * cos(n));
    }
    xsq = x * x;
    for (n = 0, d = 0, i = 9; i >= 0; i--) {
        n = n * xsq + p4[i];
        d = d * xsq + q4[i];
    }
    return x * n / d + tpi * (j1(x) * log(x) - 1. / x);
}
