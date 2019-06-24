#!/bin/bash
if ssh -o PreferredAuthentications=publickey $HOSTNAME /bin/true exit ; then
    echo "SSH is properly setup for functional testing"
else
    echo "SSH is not properly setup for functional testing"
    echo "Attempting to diagnose issue..."
    if cd ~/.ssh ; then
        if test -f ./id_rsa.pub ; then
            echo "Public ssh key exists..."
            YOUR_KEY="$(cat ./id_rsa.pub)"
            CURRENT_KEYS="./authorized_keys"
            if grep -Fxq "$YOUR_KEY" "$CURRENT_KEYS" ; then
                echo "Public key present in authorized keys. Failed to determine cause of failure"
            else
                echo "Public key not present in authorized keys file. Considering adding it to it using cat id_rsa.pub >> authorized_keys"
                exit 1
                #$YOUR_KEY >> ./authorized_keys
                #if ssh -o PreferredAuthentications=publickey $HOSTNAME /bin/true exit ; then
                #    echo "SSH now properly configured"
                #    exit 0
                #else
                #    echo "Reason for continued failure unknown"
                #    exit 0
                #fi
            fi
        else
            echo "Public ssh key does not exist. Consider making one using githubs ssh key creator"
            exit 1
        fi
    else
        echo ".ssh directory does not exist"
        exit 1
    fi
fi
