/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * j0(x), y0(x) -- Bessel functions of the first and second kinds, order zero.
 * v7 extensions, not C11; kept because v7's libm has them.
 *
 * j0 is finite everywhere; y0 is a domain error for x <= 0 (EDOM, -HUGE_VAL).  Below
 * x = 8 each is a rational fit; above it, the asymptotic form through pzero/qzero,
 * sqrt, sin and cos.  Coefficients are Hart & Cheney (#5849, #6549, #6949, #6245).
 *
 * THE COEFFICIENTS ARE SCALED.  v7's p1/q1 and p4/q4 lead with values near 5e20 and
 * 4e20 -- past this machine's largest finite 9.22e18, so they cannot even be written
 * as literals here.  Each such pair is divided by 10^10 (its trailing constant, v7's
 * `1.0', becomes 1e-10), which is a pure shift of every literal's decimal exponent and
 * leaves the ratio n/d -- the only thing either polynomial contributes -- unchanged.
 * The comment on each array records the factor.  p2/q2 and p3/q3 already fit and are
 * v7's outright.
 *
 * pzero/qzero are file-scope statics that asympt() fills and j0/y0 read, which is why
 * the four routines share one file: the same arrangement malloc.c and ctime.c take in
 * libc for statics shared between entry points.
 */
#include <errno.h>
#include <math.h>

static double pzero, qzero;
static double tpi = .6366197723675813430755350535e0;
static double pio4 = .7853981633974483096156608458e0;

static double p1[] = {
    /* scaled by 1e-10 */
    0.4933787251794133561816813446e11,   -.1179157629107610536038440800e11,
    0.6382059341072356562289432465e9,    -.1367620353088171386865416609e8,
    0.1434354939140344111664316553e6,    -.8085222034853793871199468171e3,
    0.2507158285536881945555156435e1,    -.4050412371833132706360663322e-2,
    0.2685786856980014981415848441e-5,
};
static double q1[] = {
    /* scaled by 1e-10 */
    0.4933787251794133562113278438e11,   0.5428918384092285160200195092e9,
    0.3024635616709462698627330784e7,    0.1127756739679798507056031594e5,
    0.3123043114941213172572469442e2,    0.6699987672982239671814028660e-1,
    0.1114636098462985378182402543e-3,   0.1363063652328970604442810507e-6,
    1.0e-10,
};
static double p2[] = {
    0.5393485083869438325262122897e7,    0.1233238476817638145232406055e8,
    0.8413041456550439208464315611e7,    0.2016135283049983642487182349e7,
    0.1539826532623911470917825993e6,    0.2485271928957404011288128951e4,
    0.0,
};
static double q2[] = {
    0.5393485083869438325560444960e7,    0.1233831022786324960844856182e8,
    0.8426449050629797331554404810e7,    0.2025066801570134013891035236e7,
    0.1560017276940030940592769933e6,    0.2615700736920839685159081813e4,
    1.0e0,
};
static double p3[] = {
    -.3984617357595222463506790588e4,    -.1038141698748464093880530341e5,
    -.8239066313485606568803548860e4,    -.2365956170779108192723612816e4,
    -.2262630641933704113967255053e3,    -.4887199395841261531199129300e1,
    0.0,
};
static double q3[] = {
    0.2550155108860942382983170882e6,    0.6667454239319826986004038103e6,
    0.5332913634216897168722255057e6,    0.1560213206679291652539287109e6,
    0.1570489191515395519392882766e5,    0.4087714673983499223402830260e3,
    1.0e0,
};
static double p4[] = {
    /* scaled by 1e-10 */
    -.2750286678629109583701933175e10,   0.6587473275719554925999402049e10,
    -.5247065581112764941297350814e9,    0.1375624316399344078571335453e8,
    -.1648605817185729473122082537e6,    0.1025520859686394284509167421e4,
    -.3436371222979040378171030138e1,    0.5915213465686889654273830069e-2,
    -.4137035497933148554125235152e-5,
};
static double q4[] = {
    /* scaled by 1e-10 */
    0.3726458838986165881989980e11,      0.4192417043410839973904769661e9,
    0.2392883043499781857439356652e7,    0.9162038034075185262489147968e4,
    0.2613065755041081249568482092e2,    0.5795122640700729537480087915e-1,
    0.1001702641288906265666651753e-3,   0.1282452772478993804176329391e-6,
    1.0e-10,
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

double j0(double arg)
{
    double argsq, n, d;
    int i;

    if (arg < 0.)
        arg = -arg;
    if (arg > 8.) {
        asympt(arg);
        n = arg - pio4;
        return sqrt(tpi / arg) * (pzero * cos(n) - qzero * sin(n));
    }
    argsq = arg * arg;
    for (n = 0, d = 0, i = 8; i >= 0; i--) {
        n = n * argsq + p1[i];
        d = d * argsq + q1[i];
    }
    return n / d;
}

double y0(double arg)
{
    double argsq, n, d;
    int i;

    if (arg <= 0.) {
        errno = EDOM;
        return -HUGE_VAL;
    }
    if (arg > 8.) {
        asympt(arg);
        n = arg - pio4;
        return sqrt(tpi / arg) * (pzero * sin(n) + qzero * cos(n));
    }
    argsq = arg * arg;
    for (n = 0, d = 0, i = 8; i >= 0; i--) {
        n = n * argsq + p4[i];
        d = d * argsq + q4[i];
    }
    return n / d + tpi * j0(arg) * log(arg);
}
